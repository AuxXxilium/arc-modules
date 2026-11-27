/* arc-modules: netif_trans_update() (4.7), as the kernel later defines it */
static inline void netif_trans_update(struct net_device *dev)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, 0);

	if (txq->trans_start != jiffies)
		txq->trans_start = jiffies;
}
