/* arc-modules: eth_hw_addr_set() (5.15), as the kernel later defines it */
static inline void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
	memcpy(dev->dev_addr, addr, ETH_ALEN);
}
