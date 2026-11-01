# YukiZygisk

<img align="right" src="webui/icon.svg" width="220" alt="YukiZygisk logo">

**English** | [简体中文](README.zh-CN.md)

YukiZygisk is a standalone, kernel-based Zygisk implementation for rooted Android devices. It runs Zygisk and ZN native modules, with a built-in WebUI for checking status and changing settings.

[Download](https://github.com/Rouyashiki/YukiZygisk/releases/latest) · [Report an issue](https://github.com/Rouyashiki/YukiZygisk/issues)

## Requirements

- An ARM64 device with a supported GKI kernel. Packages cover supported variants of 5.10, 5.15, 6.1, 6.6, 6.12 and 6.18; the installer checks for a matching kernel automatically.
- KernelSU, YukiSU or APatch. Magisk and devices running multiple root implementations are currently unsupported.
- A manager with module WebUI support to use the settings interface.

## Installation

1. Disable any other Zygisk implementation, including YukiSU's built-in YukiZygisk if enabled.
2. Download `YukiZygisk-…-all-kmi-arm64-v8a.zip` from [Releases](https://github.com/Rouyashiki/YukiZygisk/releases/latest). Choose the module ZIP, not the source code archive.
3. Open your root manager, go to **Modules**, and install the ZIP from local storage.
4. Reboot, then open YukiZygisk's **WebUI** from the module page to check its status.

If the installer cannot identify your kernel's KMI, use **Volume Up** to cycle through the packaged KMIs and **Volume Down** to confirm. After installation, only the selected KMI's kernel module is kept to save space. Reinstall after switching kernels if the KMI changes or needs manual selection again.

## Usage

- Install the Zygisk or ZN native modules you want through your root manager, then reboot.
- Use the WebUI to view loaded modules, check injection status and adjust settings.
- Use the module's **Action** button to view its current status and recent startup logs.
- Update, disable or uninstall YukiZygisk through your root manager, then reboot to apply the change.

If something goes wrong, [open an issue](https://github.com/Rouyashiki/YukiZygisk/issues) with your device model, Android and kernel versions, root implementation, steps to reproduce and the output from the Action button.

## OnePlus 8 (Linux 4.19)

This fork adds a device-specific, non-GKI target called `oneplus8-4.19` for the OnePlus 8 family (`instantnoodle`, `instantnoodlep`, `kebab`) on LineageOS 24.0, whose vendor kernel is Linux 4.19.325.

- The target carries KMI id `9` and the `oneplus8-4.19` identity string inside the LKM. The installer picks it without any volume-key prompt when `ro.product.device` matches and the kernel release starts with `4.19.`.
- Build locally with a prepared vendor kernel tree (`vmlinux` plus `Module.symvers`):

  ```sh
  ./build.sh kernel  --kmi oneplus8-4.19 --kdir out
  ./build.sh package --kmi oneplus8-4.19 --kdir out
  ```

- The dedicated workflow [`.github/workflows/oneplus8-4.19.yml`](.github/workflows/oneplus8-4.19.yml) builds the LKM from `LineageOS/android_kernel_oneplus_sm8250@lineage-24.0`, verifies every symbol against `System.map`, then packages a single-KMI ZIP. Without a `GPG_PRIVATE_KEY` secret it falls back to a throw-away development signing key so the artifact stays installable.
- 4.19 compatibility work lives in `kernel/compat/legacy.h` and the individual call sites: SELinux policy edits go through `security_load_policy()`, mount cleanup uses the private `do_umount()` instead of `path_umount()`, nofault reads fall back to `probe_kernel_read()`, and `bprm` exec-fd access uses `interp_flags`/`interp_data`.

Limitations:

- The module links against a specific kernel build. modversions CRCs and structure layouts must match the running kernel, so rebuild after switching kernel branches or configurations; the bundled loader only repairs the vermagic string.
- Mount cleanup depends on the private `do_umount()` symbol being present in kallsyms. It degrades to a no-op with a single `pr_warn()` line when it is missing.
- Only `LineageOS` kernels with `CONFIG_MODULES`, `CONFIG_MODVERSIONS` and `CONFIG_SECURITY_SELINUX` are tested.

## License

Original work is licensed under [Apache-2.0](LICENSE); [GPL-2.0](LICENSE-GPL-2.0) applies when used with the Linux kernel. Third-party components retain their original licenses. See [NOTICE](NOTICE) for credits and details.
