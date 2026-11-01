#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# YukiZygisk module KMI detection fixtures.
#
# License: Apache-2.0
#
# Author: Anatdx

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "$PROJECT_ROOT/module/common.sh"

check_kmi() {
	local release="$1"
	local expected="$2"
	local actual
	actual="$(yz_detect_kmi "$release")"
	[[ "$actual" == "$expected" ]] || {
		printf 'release %s: expected %s, got %s\n' "$release" "$expected" "$actual" >&2
		exit 1
	}
}

check_kmi "5.10.198-android12-9-g123456789abc-ab12345678" "android12-5.10"
check_kmi "5.10.218-android13-4-00001-gabcdef" "android13-5.10"
check_kmi "5.15.153-android14-11-gki" "android14-5.15"
check_kmi "6.1.115-android14-11-gki" "android14-6.1"
check_kmi "6.6.56-android15-8-gki" "android15-6.6"
check_kmi "6.12.23-android16-3-gki" "android16-6.12"

# Non-GKI OnePlus 8 family kernels (Linux 4.19) have no androidNN tag; the
# installer identifies them by release and device codename.
getprop() { printf '%s\n' "${YZ_TEST_DEVICE:-}"; }
YZ_TEST_DEVICE=instantnoodle check_kmi "4.19.325-perf-g12345678" "oneplus8-4.19"
YZ_TEST_DEVICE=kebab check_kmi "4.19.325-perf-g12345678" "oneplus8-4.19"
YZ_TEST_DEVICE=someotherdevice
if yz_detect_kmi "4.19.325-perf-g12345678" >/dev/null; then
	echo "non-OnePlus 4.19 release unexpectedly produced a KMI" >&2
	exit 1
fi
unset YZ_TEST_DEVICE

if yz_detect_kmi "6.6.56-vendor-kernel" >/dev/null; then
	echo "non-GKI release unexpectedly produced a KMI" >&2
	exit 1
fi

if ! printf '%s\n' \
	'Module                  Size  Used by' \
	'kernelsu              123456  0' | yz_module_list_has kernelsu; then
	echo "kernelsu lsmod entry was not detected" >&2
	exit 1
fi

if printf '%s\n' \
	'Module                  Size  Used by' \
	'not_kernelsu          123456  0' | yz_module_list_has kernelsu; then
	echo "non-KernelSU module unexpectedly matched kernelsu" >&2
	exit 1
fi

echo "module KMI detection fixtures passed"
