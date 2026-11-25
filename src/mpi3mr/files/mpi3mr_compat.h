/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Compatibility shims for building the v6.8 mpi3mr driver on 5.10.
 *
 * The driver source is taken verbatim from Linux v6.8 (8.5.1.0.0) so that it
 * stays diffable against upstream. Everything that 5.10 spells differently is
 * bridged here rather than by editing the driver, with one exception noted in
 * mpi3mr_os.c (.shost_groups, which has no macro-level equivalent).
 */
#ifndef MPI3MR_COMPAT_H_INCLUDED
#define MPI3MR_COMPAT_H_INCLUDED

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_common.h>

/*
 * 5.17 turned the per-command ->scsi_done pointer into a global scsi_done()
 * (commit 8bdf70e1a672) and added scsi_cmd_to_rq() to hide ->request
 * (commit 8b3238cabd5f). 5.10 still has both members.
 */
static inline void scsi_done(struct scsi_cmnd *cmd)
{
	cmd->scsi_done(cmd);
}

static inline struct request *scsi_cmd_to_rq(struct scsi_cmnd *cmd)
{
	return cmd->request;
}

/*
 * scsi_build_sense() (commit f2b1e9c6f867) wraps scsi_build_sense_buffer() and
 * additionally sets the result. mpi3mr only ever calls it on the error path in
 * mpi3mr_qcmd(), where SAM_STAT_CHECK_CONDITION is what we want.
 */
static inline void scsi_build_sense(struct scsi_cmnd *cmd, int desc,
				    u8 key, u8 asc, u8 ascq)
{
	scsi_build_sense_buffer(desc, cmd->sense_buffer, key, asc, ascq);
	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;
}

#endif /* < 5.17 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
/*
 * Flat shost attribute array for .shost_attrs, which 5.18 replaced with the
 * .shost_groups attribute_group list that mpi3mr_app.c builds. Kept in sync by
 * hand with mpi3mr_host_attrs[] in mpi3mr_app.c; only referenced from the
 * scsi_host_template in mpi3mr_os.c.
 */
extern struct device_attribute *mpi3mr_host_attrs_compat[];
#endif /* < 5.18 */

/*
 * busy_tag_iter_fn lost its trailing "bool reserved" argument in 5.16
 * (commit 2dd6532e9591). The driver declares the 5.16+ two-argument form, so
 * on older kernels the extra parameter is appended here and ignored. Callers
 * pass the function by name only, so nothing else needs to change.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#define MPI3MR_TAG_ITER_RESERVED_ARG	, bool reserved
#else
#define MPI3MR_TAG_ITER_RESERVED_ARG
#endif

/*
 * scsi_host_template.map_queues returned int until 5.16 (commit 4dc46e8f4a3d
 * made it void along with the blk-mq poll-queue rework).
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#define MPI3MR_MAP_QUEUES_RET		int
#define MPI3MR_MAP_QUEUES_RETURN	return 0
#else
#define MPI3MR_MAP_QUEUES_RET		void
#define MPI3MR_MAP_QUEUES_RETURN	do { } while (0)
#endif

/* scsi_host_alloc() takes a const template only from 5.18 onwards. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
#define MPI3MR_HOST_TEMPLATE_CONST
#else
#define MPI3MR_HOST_TEMPLATE_CONST	const
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
#include <linux/t10-pi.h>

/*
 * scsi_prot_ref_tag() gained its scsi_cmnd form in 5.15; before that the same
 * calculation lived in t10_pi_ref_tag(), taking the request. Both apply the
 * logical-block-size shift, so this is a straight forward to the old helper.
 */
static inline u32 scsi_prot_ref_tag(struct scsi_cmnd *scmd)
{
	return t10_pi_ref_tag(scsi_cmd_to_rq(scmd));
}
#endif /* < 5.15 */

#endif /* MPI3MR_COMPAT_H_INCLUDED */
