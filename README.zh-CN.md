# YukiZygisk

<img align="right" src="webui/icon.svg" width="220" alt="YukiZygisk logo">

[English](README.md) | **简体中文**

YukiZygisk 是一个独立的、基于内核的 Zygisk 实现，让已 root 的 Android 设备运行 Zygisk 模块，也支持 ZN 原生模块。内置 WebUI，可以查看运行状态和调整设置。

[下载](https://github.com/Rouyashiki/YukiZygisk/releases/latest) · [反馈问题](https://github.com/Rouyashiki/YukiZygisk/issues)

## 使用要求

- ARM64 设备，使用受支持的 GKI 内核。安装包覆盖 5.10、5.15、6.1、6.6、6.12 和 6.18 的受支持版本，安装时会自动检查是否匹配。
- 已安装 KernelSU、YukiSU 或 APatch。目前不支持 Magisk，也不支持多种 root 方案共存。
- 使用支持模块 WebUI 的管理器，以便打开设置界面。

## 安装

1. 关闭其他 Zygisk 实现；如果启用了 YukiSU 内置的 YukiZygisk，也需要先关闭。
2. 从 [Releases](https://github.com/Rouyashiki/YukiZygisk/releases/latest) 下载 `YukiZygisk-…-all-kmi-arm64-v8a.zip` 模块安装包，不要下载源码压缩包。
3. 打开 root 管理器，进入**模块**页面，选择从本地安装这个 ZIP。
4. 重启设备，在模块页面打开 YukiZygisk 的 **WebUI**，确认运行状态。

如果安装时无法识别内核的 KMI，按**音量上键**循环切换安装包内的 KMI，按**音量下键**确认。安装完成后仅保留选中 KMI 的内核模块，节省存储空间。更换内核后，若 KMI 改变或仍需手动选择，请重新安装模块。

## 使用

- 需要使用的 Zygisk 或 ZN 原生模块，照常通过 root 管理器安装，然后重启。
- 在 WebUI 中查看已加载的模块、注入状态和调整设置。
- 点击模块的**操作（Action）**按钮，查看当前状态和最近的启动日志。
- 更新、停用或卸载 YukiZygisk，都在 root 管理器中操作，完成后重启生效。

遇到问题时，请[提交 Issue](https://github.com/Rouyashiki/YukiZygisk/issues)，附上机型、Android 与内核版本、root 方案、复现步骤，以及操作按钮显示的输出。

## 一加 8（Linux 4.19）

本 fork 新增了设备专属的非 GKI 目标 `oneplus8-4.19`，面向一加 8 系列（`instantnoodle`、`instantnoodlep`、`kebab`）的 LineageOS 24.0，对应内核为 Linux 4.19.325。

- 目标的 KMI id 为 `9`，LKM 内置 `oneplus8-4.19` 身份字符串。当 `ro.product.device` 匹配且内核版本以 `4.19.` 开头时，安装脚本直接选中它，不再弹出音量键选择。
- 本地构建需要先准备好 vendor 内核树（含 `vmlinux` 与 `Module.symvers`）：

  ```sh
  ./build.sh kernel  --kmi oneplus8-4.19 --kdir out
  ./build.sh package --kmi oneplus8-4.19 --kdir out
  ```

- 专属工作流 [`.github/workflows/oneplus8-4.19.yml`](.github/workflows/oneplus8-4.19.yml) 会基于 `LineageOS/android_kernel_oneplus_sm8250@lineage-24.0` 编译 LKM，用 `System.map` 逐一校验符号，再打包单 KMI 的 ZIP。若仓库未配置 `GPG_PRIVATE_KEY` 密钥，会自动生成一次性开发签名密钥，保证产物仍可安装。
- 4.19 适配集中在 `kernel/compat/legacy.h` 与各处调用点：SELinux 策略修改走 `security_load_policy()`，挂载清理改用内核私有的 `do_umount()` 而非 `path_umount()`，nofault 读取回退到 `probe_kernel_read()`，`bprm` 的 exec fd 改用 `interp_flags`/`interp_data`。

已知限制：

- 内核模块与具体内核构建绑定。modversions CRC 与结构体布局必须和运行内核一致，切换内核分支或配置后需要重新编译；内置 loader 只会修正 vermagic 字符串。
- 挂载清理依赖 kallsyms 中存在私有符号 `do_umount()`；若缺失会自动退化为空操作，只打印一条 `pr_warn()` 日志。
- 仅在同时启用 `CONFIG_MODULES`、`CONFIG_MODVERSIONS`、`CONFIG_SECURITY_SELINUX` 的 LineageOS 内核上验证过。

## 许可证

原创代码采用 [Apache-2.0](LICENSE)，用于 Linux 内核时适用 [GPL-2.0](LICENSE-GPL-2.0)。第三方组件保留各自的许可证，鸣谢和详细说明见 [NOTICE](NOTICE)。
