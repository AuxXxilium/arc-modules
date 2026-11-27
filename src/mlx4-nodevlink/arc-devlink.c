// SPDX-License-Identifier: GPL-2.0-only
/*
 * arc-modules: a private devlink for drivers on a kernel built without
 * NET_DEVLINK (geminilakenk). The driver keeps its devlink calls; this file,
 * linked into the module and exporting nothing, gives them a devlink that
 * holds the driver's private area (devlink_alloc()/devlink_priv(), which the
 * driver lives in) and accepts everything else without a user interface:
 * no "devlink" netlink family, so no ports, parameters, regions or health
 * reports to read from user space. Parameters keep the module parameters'
 * values; region snapshots are freed right away.
 */
#include <linux/slab.h>
#include <linux/err.h>
#include <net/devlink.h>

struct arc_devlink_region {
	const struct devlink_region_ops *ops;
};

struct arc_devlink_reporter {
	void *priv;
};

struct devlink *devlink_alloc(const struct devlink_ops *ops, size_t priv_size)
{
	struct devlink *devlink = kzalloc(sizeof(*devlink) + priv_size, GFP_KERNEL);

	if (!devlink)
		return NULL;
	devlink->ops = ops;
	INIT_LIST_HEAD(&devlink->port_list);
	mutex_init(&devlink->lock);
	return devlink;
}

void devlink_free(struct devlink *devlink)
{
	mutex_destroy(&devlink->lock);
	kfree(devlink);
}

int devlink_register(struct devlink *devlink, struct device *dev)
{
	devlink->dev = dev;
	devlink->registered = 1;
	return 0;
}

void devlink_unregister(struct devlink *devlink)
{
	devlink->registered = 0;
}

void devlink_reload_enable(struct devlink *devlink)
{
}

void devlink_reload_disable(struct devlink *devlink)
{
}

int devlink_params_register(struct devlink *devlink, const struct devlink_param *params,
			    size_t params_count)
{
	return 0;
}

void devlink_params_unregister(struct devlink *devlink, const struct devlink_param *params,
			       size_t params_count)
{
}

void devlink_params_publish(struct devlink *devlink)
{
}

int devlink_param_driverinit_value_get(struct devlink *devlink, u32 param_id,
				       union devlink_param_value *init_val)
{
	return -EOPNOTSUPP;
}

int devlink_param_driverinit_value_set(struct devlink *devlink, u32 param_id,
				       union devlink_param_value init_val)
{
	return 0;
}

void devlink_param_value_changed(struct devlink *devlink, u32 param_id)
{
}

int devlink_port_register(struct devlink *devlink, struct devlink_port *devlink_port,
			  unsigned int port_index)
{
	devlink_port->devlink = devlink;
	devlink_port->index = port_index;
	devlink_port->registered = 1;
	return 0;
}

void devlink_port_unregister(struct devlink_port *devlink_port)
{
	devlink_port->registered = 0;
}

void devlink_port_type_eth_set(struct devlink_port *devlink_port, struct net_device *netdev)
{
}

void devlink_port_type_ib_set(struct devlink_port *devlink_port, struct ib_device *ibdev)
{
}

void devlink_port_type_clear(struct devlink_port *devlink_port)
{
}

struct devlink_region *devlink_region_create(struct devlink *devlink,
					     const struct devlink_region_ops *ops,
					     u32 region_max_snapshots, u64 region_size)
{
	struct arc_devlink_region *region = kzalloc(sizeof(*region), GFP_KERNEL);

	if (!region)
		return ERR_PTR(-ENOMEM);
	region->ops = ops;
	return (struct devlink_region *)region;
}

void devlink_region_destroy(struct devlink_region *region)
{
	kfree(region);
}

int devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	*id = 1;
	return 0;
}

void devlink_region_snapshot_id_put(struct devlink *devlink, u32 id)
{
}

/* the snapshot owns data: nothing keeps it here */
int devlink_region_snapshot_create(struct devlink_region *region, u8 *data, u32 snapshot_id)
{
	((struct arc_devlink_region *)region)->ops->destructor(data);
	return 0;
}

struct devlink_health_reporter *
devlink_health_reporter_create(struct devlink *devlink,
			       const struct devlink_health_reporter_ops *ops,
			       u64 graceful_period, void *priv)
{
	struct arc_devlink_reporter *reporter = kzalloc(sizeof(*reporter), GFP_KERNEL);

	if (!reporter)
		return ERR_PTR(-ENOMEM);
	reporter->priv = priv;
	return (struct devlink_health_reporter *)reporter;
}

void devlink_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	kfree(reporter);
}

int devlink_health_report(struct devlink_health_reporter *reporter, const char *msg,
			  void *priv_ctx)
{
	return 0;
}

int devlink_fmsg_binary_pair_put(struct devlink_fmsg *fmsg, const char *name,
				 const void *value, u32 value_len)
{
	return 0;
}

int devlink_fmsg_string_pair_put(struct devlink_fmsg *fmsg, const char *name, const char *value)
{
	return 0;
}

int devlink_fmsg_u32_pair_put(struct devlink_fmsg *fmsg, const char *name, u32 value)
{
	return 0;
}

int devlink_fmsg_u8_pair_put(struct devlink_fmsg *fmsg, const char *name, u8 value)
{
	return 0;
}
