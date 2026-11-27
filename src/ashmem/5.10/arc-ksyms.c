// SPDX-License-Identifier: GPL-2.0-only
/*
 * arc-modules: shmem_zero_setup(), which ashmem uses and the official 5.10
 * vmlinux has but does not export (ashmem is a bool there). The module finds
 * it by name when it loads (kallsyms_lookup_name(), itself not exported
 * since 5.7, is found through a kprobe) and this wrapper, with the kernel's
 * name and prototype, calls it.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>

static int (*arc_p_shmem_zero_setup)(struct vm_area_struct *);

int shmem_zero_setup(struct vm_area_struct *vma)
{
	return arc_p_shmem_zero_setup(vma);
}

int arc_ksyms_init(void)
{
	struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
	unsigned long (*kln)(const char *name);
	int ret = register_kprobe(&kp);

	if (ret < 0) {
		pr_err("ashmem_linux: cannot find kallsyms_lookup_name (%d)\n", ret);
		return ret;
	}
	kln = (void *)kp.addr;
	unregister_kprobe(&kp);
	arc_p_shmem_zero_setup = (void *)kln("shmem_zero_setup");
	if (!arc_p_shmem_zero_setup) {
		pr_err("ashmem_linux: shmem_zero_setup not found in this kernel\n");
		return -ENOENT;
	}
	return 0;
}
