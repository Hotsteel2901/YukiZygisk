/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * YukiZygisk - Host mount namespace cleanup adapter.
 *
 * Derived from YukiSU kernel_umount helpers.
 *
 * License: GPL-2.0-only
 *
 * Author: Anatdx
 */

#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/task_work.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#include <uapi/linux/mount.h>
#endif

#include "host/host.h"
#include "core/auth.h"
#include "host/mount.h"
#include "host/runtime.h"

#define YZ_PER_USER_RANGE 100000
#define YZ_AID_WEBVIEW_ZYGOTE 1053
#define YZ_UMOUNT_MAX_TARGETS 512
#define YZ_MOUNTINFO_BUF (256 * 1024)

typedef int (*yz_path_umount_fn)(struct path *path, int flags);

static yz_path_umount_fn yz_path_umount_ptr;

#if YZ_KERNEL_LEGACY
/*
 * Linux 4.19 has no path_umount().  The only equivalent entry point is the
 * private do_umount(struct mount *, int), which takes namespace_sem itself and
 * consumes no path reference.  struct mount is not part of the module-visible
 * headers, so the wrapper base is located at run time by matching the embedded
 * mnt_root/mnt_sb pair; a hard-coded offsetof() would break under randstruct or
 * a vendor layout change.
 */
#define YZ_REAL_MOUNT_SCAN_MAX 4096UL

static int (*yz_do_umount_ptr)(void *mnt, int flags);

static void *yz_real_mount_base(struct vfsmount *vmnt,
				const struct dentry *root,
				const struct super_block *sb)
{
	unsigned long offset;

	for (offset = sizeof(void *); offset < YZ_REAL_MOUNT_SCAN_MAX;
	     offset += sizeof(void *)) {
		unsigned long base = (unsigned long)vmnt - offset;
		struct {
			unsigned long root;
			unsigned long sb;
		} pair;
		unsigned long parent;

		if (!yz_kernel_read_nofault(&pair, base, sizeof(pair)))
			continue;
		if (pair.root != (unsigned long)root ||
		    pair.sb != (unsigned long)sb)
			continue;
		/* mnt_hash precedes mnt_parent, so the parent slot is always
		 * two pointers into the wrapper.  Reject candidates whose
		 * parent slot is not a kernel address. */
		if (!yz_kernel_read_nofault(&parent, base + 2 * sizeof(void *),
					    sizeof(parent)))
			continue;
		if (!yz_valid_kernel_addr(parent))
			continue;
		return (void *)base;
	}
	return NULL;
}

static YZ_INDIRECT_CALL int yz_try_umount_legacy(struct path *path, int flags)
{
	void *mnt;

	mnt = yz_real_mount_base(path->mnt, path->dentry, path->mnt->mnt_sb);
	if (!mnt) {
		pr_info("yukizygisk: cannot locate struct mount for umount\n");
		return -ENOENT;
	}
	return yz_do_umount_ptr(mnt, flags);
}
#endif /* YZ_KERNEL_LEGACY */

static bool yz_mount_is_appuid(uid_t uid)
{
	return uid == YZ_AID_WEBVIEW_ZYGOTE ||
	       uid % YZ_PER_USER_RANGE >= 10000;
}

static int yz_mount_resolve_path_umount(void)
{
	unsigned long addr;

	if (yz_path_umount_ptr)
		return 0;

	addr = yz_lookup_callable_quiet("path_umount");
	if (addr) {
		yz_path_umount_ptr = (void *)addr;
		return 0;
	}

#if YZ_KERNEL_LEGACY
	if (!yz_do_umount_ptr) {
		/* do_umount() is static, so only kallsyms can reach it. */
		yz_do_umount_ptr = (void *)yz_lookup_callable_quiet("do_umount");
	}
	if (yz_do_umount_ptr)
		return 0;
#endif

	pr_warn("yukizygisk: no umount entry point; mount cleanup disabled\n");
	return -ENOENT;
}

static YZ_INDIRECT_CALL int yz_try_umount(const char *mnt, int flags)
{
	struct path path;
	int ret;

	if (!mnt || !mnt[0])
		return -EINVAL;

	ret = yz_mount_resolve_path_umount();
	if (ret)
		return ret;

	ret = yz_kern_path(mnt, 0, &path);
	if (ret)
		return ret;

	if (path.dentry != path.mnt->mnt_root) {
		yz_path_put(&path);
		return -EINVAL;
	}

	if (yz_path_umount_ptr) {
		/* path_umount() consumes the successful kern_path() reference. */
		ret = yz_path_umount_ptr(&path, flags);
	} else {
#if YZ_KERNEL_LEGACY
		/* do_umount() leaves the reference with its caller. */
		ret = yz_try_umount_legacy(&path, flags);
#else
		ret = -ENOENT;
#endif
		yz_path_put(&path);
	}
	if (ret)
		pr_info("yukizygisk: umount %s failed: %d\n", mnt, ret);
	return ret;
}

static bool yz_mount_is_module(const char *root, const char *target,
			       const char *source, const char *super)
{
	/* Standalone YukiZygisk owns this cleanup path. Match KSU-style
	 * mount-source tags plus the wider ZN/YZ-style roots that point back
	 * into /data/adb, then detach them here without depending on KSU's
	 * kernel_umount feature switch. */
	if (root && !strncmp(root, "/adb/modules", 12))
		return true;
	if (target && !strncmp(target, "/data/adb/", 10))
		return true;
	if (source && (!strcmp(source, "KSU") || !strcmp(source, "YukiSU") ||
		       !strcmp(source, "magisk") ||
		       !strcmp(source, "APatch") ||
		       !strcmp(source, "YukiZygisk")))
		return true;
	if (super && (strstr(super, "/adb/modules") ||
		      strstr(super, "/data/adb/")))
		return true;
	return false;
}

static bool yz_parse_mountinfo(char *line, char **root, char **target,
			       char **source, char **super)
{
	char *tok;
	char *f3 = NULL;
	char *f4 = NULL;
	int n = 0;

	while ((tok = strsep(&line, " ")) != NULL) {
		if (n == 3)
			f3 = tok;
		else if (n == 4)
			f4 = tok;
		else if (n >= 6 && !strcmp(tok, "-")) {
			char *fstype = strsep(&line, " ");
			char *src = strsep(&line, " ");

			if (!fstype || !src || !f3 || !f4)
				return false;
			*root = f3;
			*target = f4;
			*source = src;
			*super = strsep(&line, " ");
			return true;
		}
		n++;
	}
	return false;
}

static int yz_umount_scan_mountinfo(void)
{
	struct file *file;
	char *buf;
	char **targets;
	char *p;
	char *line;
	loff_t pos = 0;
	size_t total = 0;
	int count = 0;
	int i;
	int ret = 0;

	buf = vmalloc(YZ_MOUNTINFO_BUF);
	if (!buf)
		return -ENOMEM;

	targets = kmalloc_array(YZ_UMOUNT_MAX_TARGETS, sizeof(*targets),
				GFP_KERNEL);
	if (!targets) {
		vfree(buf);
		return -ENOMEM;
	}

	file = yz_file_open("/proc/self/mountinfo", O_RDONLY, 0);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_free;
	}

	while (total < YZ_MOUNTINFO_BUF - 1) {
		ssize_t n = yz_kernel_read(file, buf + total,
					   YZ_MOUNTINFO_BUF - 1 - total,
					   &pos);

		if (n < 0) {
			ret = n;
			break;
		}
		if (n == 0)
			break;
		total += n;
	}
	yz_file_close(file, NULL);
	if (ret)
		goto out_free;

	buf[total] = '\0';
	p = buf;
	while ((line = strsep(&p, "\n")) != NULL) {
		char *root;
		char *target;
		char *source;
		char *super;

		if (!line[0])
			continue;
		if (!yz_parse_mountinfo(line, &root, &target, &source, &super))
			continue;
		if (yz_mount_is_module(root, target, source, super) &&
		    count < YZ_UMOUNT_MAX_TARGETS)
			targets[count++] = target;
	}

	for (i = count - 1; i >= 0; i--) {
		pr_info("yukizygisk: detaching mount %s\n", targets[i]);
		yz_try_umount(targets[i], MNT_DETACH);
	}

out_free:
	kfree(targets);
	vfree(buf);
	return ret;
}

struct yz_umount_tw {
	struct callback_head cb;
	struct yz_auth_ticket ticket;
	struct mm_struct *mm;
};

static void yz_umount_tw_func(struct callback_head *cb)
{
	struct yz_umount_tw *tw = container_of(cb, struct yz_umount_tw, cb);
	const struct cred *old_cred;
	int ret;

	if (current->flags & PF_EXITING || current->mm != tw->mm ||
	    !yz_auth_ticket_begin(&tw->ticket))
		goto out;

	/* Runs as task_work in the target app, so /proc/self/mountinfo and
	 * kern_path() resolve inside that app's mount namespace. */
	old_cred = yz_host_override_creds();
	ret = yz_umount_scan_mountinfo();
	yz_host_revert_creds(old_cred);
	if (ret)
		pr_info("yukizygisk: mount cleanup pid=%d failed: %d\n",
			current->pid, ret);
	yz_auth_end();

out:
	mmdrop(tw->mm);
	kfree(tw);
	module_put(THIS_MODULE);
}

int yz_host_umount_pid(pid_t pid)
{
	struct task_struct *task;
	struct yz_umount_tw *tw;
	uid_t uid;
	int ret;

	if (pid <= 0)
		return -EINVAL;

	rcu_read_lock();
	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	rcu_read_unlock();
	if (!task)
		return -ESRCH;
	ret = yz_auth_target_task(task);
	if (ret) {
		put_task_struct(task);
		return ret;
	}

	uid = task_uid(task).val;
	if (!yz_mount_is_appuid(uid)) {
		pr_info("yukizygisk: umount_pid reject non-app pid=%d uid=%u\n",
			pid, uid);
		put_task_struct(task);
		return -EPERM;
	}

	tw = kzalloc(sizeof(*tw), GFP_KERNEL);
	if (!tw) {
		put_task_struct(task);
		return -ENOMEM;
	}

	ret = yz_auth_current_ticket(&tw->ticket);
	if (ret || !try_module_get(THIS_MODULE)) {
		kfree(tw);
		put_task_struct(task);
		return ret ?: -ENODEV;
	}
	task_lock(task);
	tw->mm = task->mm;
	if (tw->mm)
		mmgrab(tw->mm);
	task_unlock(task);
	if (!tw->mm) {
		module_put(THIS_MODULE);
		kfree(tw);
		put_task_struct(task);
		return -ESRCH;
	}
	init_task_work(&tw->cb, yz_umount_tw_func);
	ret = yz_task_work_add(task, &tw->cb, TWA_RESUME);
	if (ret) {
		mmdrop(tw->mm);
		module_put(THIS_MODULE);
		kfree(tw);
		put_task_struct(task);
		return -ESRCH;
	}

	put_task_struct(task);
	pr_info("yukizygisk: umount_pid scheduled pid=%d uid=%u\n", pid, uid);
	return 0;
}
