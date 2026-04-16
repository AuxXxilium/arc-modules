# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_DMABUF_OOT) += dmabuf.o

dmabuf-$(CONFIG_DMA_SHARED_BUFFER) := dma-buf.o dma-fence.o dma-fence-array.o dma-fence-chain.o \
	 dma-resv.o seqno-fence.o
dmabuf-$(CONFIG_DMABUF_HEAPS)	+= dma-heap.o
obj-$(CONFIG_DMABUF_HEAPS)	+= heaps/
dmabuf-$(CONFIG_SYNC_FILE)		+= sync_file.o
dmabuf-$(CONFIG_SW_SYNC)		+= sw_sync.o sync_debug.o
dmabuf-$(CONFIG_UDMABUF)		+= udmabuf.o

dmabuf_selftests-y := \
	selftest.o \
	st-dma-fence.o \
	st-dma-fence-chain.o

obj-$(CONFIG_DMABUF_SELFTESTS)	+= dmabuf_selftests.o
