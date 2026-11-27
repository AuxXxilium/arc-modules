/*
 * arc-modules: pci_alloc_irq_vectors() (4.8) for the one MSI vector tn40xx
 * asks for; pdev->irq is then that vector's irq, as the driver expects.
 */
#define PCI_IRQ_MSI (1 << 1)
static inline int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
					unsigned int max_vecs, unsigned int flags)
{
	int rc = pci_enable_msi(dev);

	return rc ? rc : 1;
}

static inline void pci_free_irq_vectors(struct pci_dev *dev)
{
	pci_disable_msi(dev);
}
