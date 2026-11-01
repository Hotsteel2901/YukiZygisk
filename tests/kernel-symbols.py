#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# YukiZygisk - verify that a target kernel can satisfy the module at runtime.
#
# The kernel module resolves almost every kernel entry point at runtime through
# kallsyms instead of linking against them, so a successful build does not prove
# that the module can initialize on a given kernel.  This checker compares
#
#   1. every undefined ELF symbol of yukizygisk.ko (link-time imports), and
#   2. every symbol name passed to yz_lookup_name()/yz_lookup_callable() in the
#      kernel sources (runtime lookups)
#
# against the target kernel's System.map.
#
# License: Apache-2.0

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]

# Symbol names passed to the runtime resolver, e.g.
#   yz_lookup_callable_quiet("policydb_write")
LOOKUP_RE = re.compile(
    r"yz_lookup_(?:name|callable)(?:_quiet)?\s*\(\s*\"([A-Za-z0-9_.]+)\""
)

# Imports that the module loader itself provides or that are resolved by the
# kernel's module loader rather than System.map.
IGNORED_IMPORTS = {
    "_mcount",
    "__gnu_mcount_nc",
    "__stack_chk_fail",
    "__stack_chk_guard",
    "module_layout",
}

# Runtime lookups that may legitimately be absent.  Each entry documents why
# the module still initializes without it; the callers already treat a failed
# lookup as a disabled optional feature.
OPTIONAL_SYMBOLS = {
    # Present only while KernelSU/YukiSU is installed.
    "ksu_syscall_table",
    "ksu_dispatcher_nr",
    "ksu_uid_should_umount",
    "ksu_get_allow_list",
    "__ksu_is_allow_uid",
    "__ksu_is_allow_uid_for_current",
    # KernelSU vmap bookkeeping; only used by its policy scanner.
    "vmap_nodes",
    "nr_vmap_nodes",
    # Present only while APatch is installed.
    "su_get_path",
    "su_allow_uid_profile",
    "get_ap_mod_exclude",
    # KernelSU/YukiSU kernel-side mount helper.  Vendor 4.19 kernels do not
    # export it; mount cleanup then stays disabled (see README).
    "path_umount",
    # Post-4.19 kernel APIs guarded by LINUX_VERSION_CODE.
    "close_fd",
    "symtab_search",
    "sidtab_search_entry",
    "task_work_cancel_match",
    "static_calls_table",
    "lsm_active_cnt",
    "dcache_clean_inval_poc",
    "caches_clean_inval_pou",
    # Fallback spelling used only when copy_from_kernel_nofault is missing.
    "probe_kernel_read",
}


def read_system_map(path: pathlib.Path) -> set[str]:
    symbols: set[str] = set()
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols.add(parts[2])
    return symbols


def read_undefined(module: pathlib.Path) -> set[str]:
    """Read undefined symbols from the ELF symbol table without external tools."""
    data = module.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{module}: not a little-endian ELF64 file")

    shoff = int.from_bytes(data[40:48], "little")
    shentsize = int.from_bytes(data[58:60], "little")
    shnum = int.from_bytes(data[60:62], "little")
    shstrndx = int.from_bytes(data[62:64], "little")

    def section(index: int) -> tuple[int, int, int, int]:
        base = shoff + index * shentsize
        name = int.from_bytes(data[base:base + 4], "little")
        kind = int.from_bytes(data[base + 4:base + 8], "little")
        offset = int.from_bytes(data[base + 24:base + 32], "little")
        size = int.from_bytes(data[base + 32:base + 40], "little")
        link = int.from_bytes(data[base + 40:base + 44], "little")
        entsize = int.from_bytes(data[base + 56:base + 64], "little")
        return name, kind, offset, size, link, entsize

    undefined: set[str] = set()
    for index in range(shnum):
        _, kind, offset, size, link, entsize = section(index)
        if kind != 2:  # SHT_SYMTAB
            continue
        if not entsize:
            continue
        strtab_off = section(link)[2]
        for pos in range(offset, offset + size, entsize):
            info = data[pos + 4]
            shndx = int.from_bytes(data[pos + 6:pos + 8], "little")
            name_off = int.from_bytes(data[pos:pos + 4], "little")
            if shndx != 0 or (info & 0xF) not in (0, 10):  # NOTYPE / GNU_IFUNC
                continue
            end = data.index(b"\0", strtab_off + name_off)
            name = data[strtab_off + name_off:end].decode("ascii", "replace")
            if name and not name.startswith(("$", ".")):
                undefined.add(name)
    return undefined


def read_runtime_lookups(source_dir: pathlib.Path) -> set[str]:
    names: set[str] = set()
    for path in source_dir.rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        for match in LOOKUP_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
            names.add(match.group(1))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--system-map", required=True, type=pathlib.Path)
    parser.add_argument("--module", required=True, type=pathlib.Path)
    parser.add_argument("--source", type=pathlib.Path, default=REPO / "kernel")
    args = parser.parse_args()

    symbols = read_system_map(args.system_map)
    if not symbols:
        print(f"{args.system_map}: empty System.map", file=sys.stderr)
        return 2

    imports = read_undefined(args.module) - IGNORED_IMPORTS
    missing_imports = sorted(name for name in imports if name not in symbols)
    runtime = read_runtime_lookups(args.source)
    missing_runtime = sorted(
        name for name in runtime
        if name not in symbols and name not in OPTIONAL_SYMBOLS
    )
    absent_optional = sorted(
        name for name in runtime
        if name not in symbols and name in OPTIONAL_SYMBOLS
    )

    print(f"System.map symbols: {len(symbols)}")
    print(f"Link-time imports:  {len(imports)}")
    print(f"Runtime lookups:    {len(runtime)}")
    if absent_optional:
        print(f"Optional absent:    {', '.join(absent_optional)}")

    failed = False
    if missing_imports:
        failed = True
        print("\nImported symbols missing from the target kernel:", file=sys.stderr)
        for name in missing_imports:
            print(f"  {name}", file=sys.stderr)
    if missing_runtime:
        failed = True
        print("\nRuntime-resolved symbols missing from the target kernel:", file=sys.stderr)
        for name in missing_runtime:
            print(f"  {name}", file=sys.stderr)

    if failed:
        return 1

    print("All module symbols are available in the target kernel.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
