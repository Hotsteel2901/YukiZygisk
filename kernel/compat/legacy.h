/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * YukiZygisk - legacy (pre-GKI, Linux 4.x) kernel portability shim.
 *
 * Force-included by Kbuild for every translation unit when the target kernel
 * is older than 5.0.  It must stay free of heavyweight kernel headers: the
 * module's crypto sources are shared with userspace builds and only tolerate
 * the smallest possible prelude.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module (or
 * linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 */
#ifndef _YUKIZYGISK_COMPAT_LEGACY_H
#define _YUKIZYGISK_COMPAT_LEGACY_H

#include <linux/version.h>
#include <linux/types.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#error "compat/legacy.h must only be used with pre-5.0 kernels"
#endif

/* Export namespaces arrived with 5.4; pre-GKI kernels have a single one. */
#ifndef MODULE_IMPORT_NS
#define MODULE_IMPORT_NS(ns)
#endif

/* enum task_work_notify_mode did not exist before 5.3. */
#ifndef TWA_RESUME
enum task_work_notify_mode {
	TWA_NONE,
	TWA_RESUME,
	TWA_SIGNAL,
};
#endif

/* arm64 defines its stack canary without CANARY_MASK before 5.x. */
#ifndef CANARY_MASK
#define CANARY_MASK 0xffffffffffffff00UL
#endif

/* CONFIG_CFI_CLANG does not exist on these kernels. */
#ifndef __nocfi
#define __nocfi
#endif

/*
 * task_struct::start_boottime was added in 5.5.  Linux 4.x keeps the same
 * clock in start_time (nanoseconds since boot); the module only compares it
 * for process-identity checks, so the boot-time offset is irrelevant.
 */
#ifdef start_boottime
#undef start_boottime
#endif
#define start_boottime start_time

#endif /* _YUKIZYGISK_COMPAT_LEGACY_H */
