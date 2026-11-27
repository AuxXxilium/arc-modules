// SPDX-License-Identifier: GPL-2.0
/*
 * arc-modules: ice 1.9.11 without devlink. Its ice_devlink.o (left out when
 * the kernel has no NET_DEVLINK) also holds the TC parameters and the MDD
 * health reporter, which ice calls whenever the kernel headers know devlink
 * health; without devlink they do nothing, as ice_devlink.h's own stubs do
 * for the rest.
 */
#include "ice.h"

int ice_devlink_tc_params_register(struct ice_vsi *vsi)
{
	return 0;
}

void ice_devlink_tc_params_unregister(struct ice_vsi *vsi)
{
}

#ifdef HAVE_DEVLINK_HEALTH
void ice_devlink_init_mdd_reporter(struct ice_pf *pf)
{
}

void ice_devlink_destroy_mdd_reporter(struct ice_pf *pf)
{
}

void ice_devlink_report_mdd_event(struct ice_pf *pf, enum ice_mdd_src src,
				  u8 pf_num, u16 vf_num, u8 event, u16 queue)
{
}

void ice_devlink_clear_after_reset(struct ice_pf *pf)
{
}
#endif /* HAVE_DEVLINK_HEALTH */
