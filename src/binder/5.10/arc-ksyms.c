// SPDX-License-Identifier: GPL-2.0-only
/*
 * arc-modules: functions binder uses that the official 5.10 vmlinux has but
 * does not export (binder is a bool there, so nothing needed them). The
 * module finds each by name when it loads (kallsyms_lookup_name(), itself
 * not exported since 5.7, is found through a kprobe) and these wrappers,
 * with the kernel's names and prototypes, call it.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/fdtable.h>
#include <linux/security.h>
#include <linux/task_work.h>
#include <linux/wait.h>
#include <linux/mm.h>

static int (*arc_p_can_nice)(const struct task_struct *, const int);
static int (*arc_p___close_fd_get_file)(unsigned int, struct file **);
static void (*arc_p_mmput_async)(struct mm_struct *);
static int (*arc_p_security_binder_set_context_mgr)(struct task_struct *);
static int (*arc_p_security_binder_transaction)(struct task_struct *, struct task_struct *);
static int (*arc_p_security_binder_transfer_binder)(struct task_struct *, struct task_struct *);
static int (*arc_p_security_binder_transfer_file)(struct task_struct *, struct task_struct *, struct file *);
static int (*arc_p_task_work_add)(struct task_struct *, struct callback_head *, enum task_work_notify_mode);
static void (*arc_p___wake_up_pollfree)(struct wait_queue_head *);
static void (*arc_p_zap_page_range)(struct vm_area_struct *, unsigned long, unsigned long);

int can_nice(const struct task_struct *p, const int nice)
{
	return arc_p_can_nice(p, nice);
}

int __close_fd_get_file(unsigned int fd, struct file **res)
{
	return arc_p___close_fd_get_file(fd, res);
}

void mmput_async(struct mm_struct *mm)
{
	arc_p_mmput_async(mm);
}

int security_binder_set_context_mgr(struct task_struct *mgr)
{
	return arc_p_security_binder_set_context_mgr(mgr);
}

int security_binder_transaction(struct task_struct *from, struct task_struct *to)
{
	return arc_p_security_binder_transaction(from, to);
}

int security_binder_transfer_binder(struct task_struct *from, struct task_struct *to)
{
	return arc_p_security_binder_transfer_binder(from, to);
}

int security_binder_transfer_file(struct task_struct *from, struct task_struct *to,
				  struct file *file)
{
	return arc_p_security_binder_transfer_file(from, to, file);
}

int task_work_add(struct task_struct *task, struct callback_head *twork,
		  enum task_work_notify_mode mode)
{
	return arc_p_task_work_add(task, twork, mode);
}

void __wake_up_pollfree(struct wait_queue_head *wq_head)
{
	arc_p___wake_up_pollfree(wq_head);
}

void zap_page_range(struct vm_area_struct *vma, unsigned long address, unsigned long size)
{
	arc_p_zap_page_range(vma, address, size);
}

#define ARC_FIND(name) do { \
	arc_p_##name = (void *)kln(#name); \
	if (!arc_p_##name) { \
		pr_err("binder_linux: %s not found in this kernel\n", #name); \
		return -ENOENT; \
	} \
} while (0)

int arc_ksyms_init(void)
{
	struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
	unsigned long (*kln)(const char *name);
	int ret = register_kprobe(&kp);

	if (ret < 0) {
		pr_err("binder_linux: cannot find kallsyms_lookup_name (%d)\n", ret);
		return ret;
	}
	kln = (void *)kp.addr;
	unregister_kprobe(&kp);
	ARC_FIND(can_nice);
	ARC_FIND(__close_fd_get_file);
	ARC_FIND(mmput_async);
	ARC_FIND(security_binder_set_context_mgr);
	ARC_FIND(security_binder_transaction);
	ARC_FIND(security_binder_transfer_binder);
	ARC_FIND(security_binder_transfer_file);
	ARC_FIND(task_work_add);
	ARC_FIND(__wake_up_pollfree);
	ARC_FIND(zap_page_range);
	return 0;
}
