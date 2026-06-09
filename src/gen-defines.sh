#!/usr/bin/env bash
# gen-defines.sh
# Generates defines.PLATFORM files for each platform by scanning the thirdparty
# directory and mapping every .ko filename to its Kconfig CONFIG_xxx symbol.
# Output files are written to src/4.x/defines.<platform> and src/5.x/defines.<platform>.

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_debug() { [[ "${VERBOSE:-0}" == "1" ]] && echo -e "${BLUE}[DEBUG]${NC} $*" || true; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
THIRDPARTY_DIR="${REPO_ROOT}/thirdparty"
PLATFORMS_FILE="${SCRIPT_DIR}/PLATFORMS"

# ---------------------------------------------------------------------------
# Platform identifier line — first line written to every defines file
# ---------------------------------------------------------------------------
declare -A PLATFORM_IDENT=(
  ["apollolake"]="CONFIG_APOLLOLAKE=y"
  ["broadwell"]="CONFIG_BROADWELL=y"
  ["broadwellnk"]="CONFIG_BROADWELLNK=y"
  ["broadwellnkv2"]="CONFIG_BROADWELLNKV2=y"
  ["broadwellntbap"]="CONFIG_BROADWELLNTBAP=y"
  ["denverton"]="CONFIG_DENVERTON=y"
  ["epyc7002"]="CONFIG_EPYC7002=y"
  ["geminilake"]="CONFIG_GEMINILAKE=y"
  ["geminilakenk"]="CONFIG_GEMINILAKENK=y"
  ["purley"]="CONFIG_PURLEY=y"
  ["r1000"]="CONFIG_R1000=y"
  ["r1000nk"]="CONFIG_R1000NK=y"
  ["v1000"]="CONFIG_V1000=y"
  ["v1000nk"]="CONFIG_V1000NK=y"
)

# ---------------------------------------------------------------------------
# Comprehensive .ko basename → CONFIG_xxx mapping
# Value "_SKIP" means the module is proprietary/out-of-tree and cannot be
# built from upstream kernel source (it will be kept from thirdparty as-is).
# ---------------------------------------------------------------------------
declare -A KO_TO_CONFIG=(
  # ── ATA / SATA ──────────────────────────────────────────────────────────
  ["piix"]="CONFIG_ATA_PIIX"
  ["sata_sil"]="CONFIG_SATA_SIL"
  ["sata_sil24"]="CONFIG_SATA_SIL24"
  ["pdc_adma"]="CONFIG_PDC_ADMA"
  ["sata_mv"]="CONFIG_SATA_MV"
  ["sata_nv"]="CONFIG_SATA_NV"
  ["sata_dwc"]="CONFIG_SATA_DWC"
  ["sata_ahci"]="CONFIG_SATA_AHCI"
  ["ahci"]="CONFIG_SATA_AHCI"
  ["ata_generic"]="CONFIG_ATA_GENERIC"

  # ── Network – Intel ─────────────────────────────────────────────────────
  ["e1000"]="CONFIG_E1000"
  ["e1000e"]="CONFIG_E1000E"
  ["igb"]="CONFIG_IGB"
  ["igbvf"]="CONFIG_IGBVF"
  ["ixgb"]="CONFIG_IXGB"
  ["ixgbe"]="CONFIG_IXGBE"
  ["ixgbevf"]="CONFIG_IXGBEVF"
  ["i40e"]="CONFIG_I40E"
  ["iavf"]="CONFIG_IAVF"
  ["ice"]="CONFIG_ICE"
  ["igc"]="CONFIG_IGC"
  ["intel_auxiliary"]="CONFIG_AUXILIARY_BUS"

  # ── Network – Realtek ───────────────────────────────────────────────────
  ["8139cp"]="CONFIG_8139CP"
  ["8139too"]="CONFIG_8139TOO"
  ["8390"]="CONFIG_8390"
  ["r8169"]="CONFIG_R8169"
  ["r8169_lk"]="CONFIG_R8169"
  ["r8168"]="CONFIG_R8168"
  ["r8168_tx"]="CONFIG_R8168"
  ["r8125"]="CONFIG_R8125"
  ["r8125_txrx"]="CONFIG_R8125"
  ["r8126"]="CONFIG_R8126"
  ["r8126_txrx"]="CONFIG_R8126"
  ["r8127"]="CONFIG_R8127"
  ["r8127_txrx"]="CONFIG_R8127"
  ["r8152"]="CONFIG_USB_RTL8152"
  ["r8153_ecm"]="CONFIG_USB_RTL8153_ECM"
  ["realtek"]="CONFIG_REALTEK_PHY"

  # ── Network – Broadcom ──────────────────────────────────────────────────
  ["b44"]="CONFIG_B44"
  ["bnx2"]="CONFIG_BNX2"
  ["bnx2x"]="CONFIG_BNX2X"
  ["bnxt_en"]="CONFIG_BNXT"
  ["tg3"]="CONFIG_TIGON3"
  ["ssb"]="CONFIG_SSB"
  ["broadcom"]="CONFIG_BROADCOM_PHY"
  ["bcm-phy-lib"]="CONFIG_BCM_NET_PHYPMDIO"

  # ── Network – Mellanox ──────────────────────────────────────────────────
  ["mlx4_core"]="CONFIG_MLX4_CORE"
  ["mlx4_en"]="CONFIG_MLX4_EN"
  ["mlx4_ib"]="CONFIG_MLX4_INFINIBAND"
  ["mlx5_core"]="CONFIG_MLX5_CORE"
  ["mlx5_ib"]="CONFIG_MLX5_INFINIBAND"
  ["mlxfw"]="CONFIG_MLXFW"
  ["mlxsw_core"]="CONFIG_MLXSW_CORE"
  ["mlxsw_pci"]="CONFIG_MLXSW_PCI"
  ["mlxsw_i2c"]="CONFIG_MLXSW_I2C"

  # ── Network – Atheros / Qualcomm ────────────────────────────────────────
  ["alx"]="CONFIG_ALX"
  ["atl1c"]="CONFIG_ATL1C"
  ["atl1e"]="CONFIG_ATL1E"

  # ── Network – Aquantia ──────────────────────────────────────────────────
  ["atlantic"]="CONFIG_AQTION"
  ["aqc111"]="CONFIG_USB_AQC111"

  # ── Network – Chelsio ───────────────────────────────────────────────────
  ["cxgb"]="CONFIG_CHELSIO_T1"
  ["cxgb3"]="CONFIG_CHELSIO_T3"
  ["cxgb4"]="CONFIG_CHELSIO_T4"
  ["cxgb4vf"]="CONFIG_CHELSIO_T4VF"

  # ── Network – Emulex / Broadcom BE ──────────────────────────────────────
  ["be2net"]="CONFIG_BE2NET"

  # ── Network – Marvell ───────────────────────────────────────────────────
  ["skge"]="CONFIG_SKGE"
  ["sky2"]="CONFIG_SKY2"

  # ── Network – QLogic / Cavium ───────────────────────────────────────────
  ["qla3xxx"]="CONFIG_QLA3XXX"
  ["qlcnic"]="CONFIG_QLCNIC"
  ["qlge"]="CONFIG_QLGE"
  ["netxen_nic"]="CONFIG_NETXEN_NIC"
  ["qed"]="CONFIG_QED"
  ["qede"]="CONFIG_QEDE"
  ["qedr"]="CONFIG_QEDR"
  ["qedf"]="CONFIG_QEDF"
  ["qedi"]="CONFIG_QEDI"

  # ── Network – Solarflare / Xilinx ───────────────────────────────────────
  ["sfc"]="CONFIG_SFC"
  ["sfc-falcon"]="CONFIG_SFC_FALCON"

  # ── Network – Via ───────────────────────────────────────────────────────
  ["via-rhine"]="CONFIG_VIA_RHINE"
  ["via-velocity"]="CONFIG_VIA_VELOCITY"
  ["jme"]="CONFIG_JME"

  # ── Network – VMware ────────────────────────────────────────────────────
  ["vmxnet3"]="CONFIG_VMXNET3"

  # ── Network – HiSilicon ─────────────────────────────────────────────────
  ["hinic"]="CONFIG_HINIC"

  # ── Network – misc / phy ────────────────────────────────────────────────
  ["ne2k-pci"]="CONFIG_NE2K_PCI"
  ["tehuti"]="CONFIG_NET_VENDOR_TEHUTI"
  ["wireguard"]="CONFIG_WIREGUARD"
  ["yt6801"]="CONFIG_YT6801"
  ["failover"]="CONFIG_NET_FAILOVER"
  ["net_failover"]="CONFIG_NET_FAILOVER"
  ["libphy"]="CONFIG_PHYLIB"
  ["mdio"]="CONFIG_MDIO"
  ["mdio_devres"]="CONFIG_MDIO_DEVICE"
  ["mii"]="CONFIG_MII"
  ["fixed_phy"]="CONFIG_FIXED_PHY"
  ["of_mdio"]="CONFIG_OF_MDIO"
  ["phy"]="CONFIG_GENERIC_PHY"
  ["forcedeth"]="CONFIG_FORCEDETH"

  # ── USB Network ─────────────────────────────────────────────────────────
  ["usbnet"]="CONFIG_USB_USBNET"
  ["asix"]="CONFIG_USB_NET_AX8817X"
  ["ax88179_178a"]="CONFIG_USB_NET_AX88179_178A"
  ["cdc_ether"]="CONFIG_USB_NET_CDCETHER"
  ["cdc_ncm"]="CONFIG_USB_NET_CDC_NCM"
  ["cx82310_eth"]="CONFIG_USB_NET_CX82310_ETH"
  ["dm9601"]="CONFIG_USB_NET_DM9601"
  ["lan78xx"]="CONFIG_USB_LAN78XX"
  ["mcs7830"]="CONFIG_USB_NET_MCS7830"
  ["rndis_host"]="CONFIG_USB_NET_RNDIS_HOST"
  ["sr9700"]="CONFIG_USB_NET_SR9700"
  ["sr9800"]="CONFIG_USB_NET_SR9800"
  ["smsc75xx"]="CONFIG_USB_SMSC75XX"
  ["smsc95xx"]="CONFIG_USB_SMSC95XX"
  ["ch9200"]="CONFIG_USB_NET_CH9200"

  # ── SCSI / SAS / RAID ───────────────────────────────────────────────────
  ["3w-9xxx"]="CONFIG_SCSI_3W_9XXX"
  ["3w-sas"]="CONFIG_SCSI_3W_SAS"
  ["aacraid"]="CONFIG_SCSI_AACRAID"
  ["aic94xx"]="CONFIG_SCSI_AIC94XX"
  ["ch"]="CONFIG_CHR_DEV_SCH"
  ["hpsa"]="CONFIG_SCSI_HPSA"
  ["isci"]="CONFIG_SCSI_ISCI"
  ["iscsi_tcp"]="CONFIG_ISCSI_TCP"
  ["libiscsi"]="CONFIG_LIBISCSI"
  ["libiscsi_tcp"]="CONFIG_LIBISCSI_TCP"
  ["libsas"]="CONFIG_SCSI_SAS_LIBSAS"
  ["megaraid"]="CONFIG_MEGARAID_NEWGEN"
  ["megaraid_mbox"]="CONFIG_MEGARAID_MAILBOX"
  ["megaraid_mm"]="CONFIG_MEGARAID_MM"
  ["megaraid_sas"]="CONFIG_MEGARAID_SAS"
  ["mpt3sas"]="CONFIG_SCSI_MPT3SAS"
  ["mptbase"]="CONFIG_FUSION"
  ["mptctl"]="CONFIG_FUSION_CTL"
  ["mptsas"]="CONFIG_FUSION_SAS"
  ["mptscsih"]="CONFIG_FUSION"
  ["mptspi"]="CONFIG_FUSION_SPI"
  ["mvsas"]="CONFIG_SCSI_MVSAS"
  ["mpi3mr"]="CONFIG_SCSI_MPI3MR"
  ["raid_class"]="CONFIG_RAID_ATTRS"
  ["scsi_debug"]="CONFIG_SCSI_DEBUG"
  ["scsi_transport_sas"]="CONFIG_SCSI_TRANSPORT_SAS"
  ["scsi_transport_spi"]="CONFIG_SCSI_SPI_ATTRS"
  ["scsi_transport_fc"]="CONFIG_SCSI_FC_ATTRS"
  ["sg"]="CONFIG_CHR_DEV_SG"
  ["smartpqi"]="CONFIG_SCSI_SMARTPQI"
  ["vmw_pvscsi"]="CONFIG_VMWARE_PVSCSI"
  ["ses"]="CONFIG_SCSI_ENCLOSURE"
  ["sd_mod"]="CONFIG_BLK_DEV_SD"
  ["sr_mod"]="CONFIG_BLK_DEV_SR"
  ["cdrom"]="CONFIG_BLK_DEV_SR"

  # ── InfiniBand / RDMA ───────────────────────────────────────────────────
  ["ib_core"]="CONFIG_INFINIBAND"
  ["ib_cm"]="CONFIG_INFINIBAND_CM"
  ["ib_mad"]="CONFIG_INFINIBAND"
  ["ib_sa"]="CONFIG_INFINIBAND"
  ["ib_addr"]="CONFIG_INFINIBAND"
  ["ib_umad"]="CONFIG_INFINIBAND_UMAD"
  ["ib_uverbs"]="CONFIG_INFINIBAND_UVERBS"
  ["iw_cm"]="CONFIG_INFINIBAND_IWCM"
  ["rdma_cm"]="CONFIG_RDMA_CM"

  # ── KVM / Virtualization ────────────────────────────────────────────────
  ["kvm"]="CONFIG_KVM"
  ["kvm-intel"]="CONFIG_KVM_INTEL"
  ["kvm-amd"]="CONFIG_KVM_AMD"
  ["irqbypass"]="CONFIG_IRQ_BYPASS_MANAGER"
  ["vfio"]="CONFIG_VFIO"
  ["vfio-pci"]="CONFIG_VFIO_PCI"
  ["vfio_iommu_type1"]="CONFIG_VFIO_IOMMU_TYPE1"
  ["vfio_virqfd"]="CONFIG_VFIO"
  ["vfio_mdev"]="CONFIG_VFIO_MDEV"
  ["mdev"]="CONFIG_VFIO_MDEV"
  ["vmw_vmci"]="CONFIG_VMWARE_VMCI"
  ["vsock"]="CONFIG_VSOCKETS"
  ["vsock_loopback"]="CONFIG_VSOCKETS_LOOPBACK"
  ["vmw_vsock_vmci_transport"]="CONFIG_VMWARE_VMCI_VSOCKETS"
  ["vmw_vsock_virtio_transport"]="CONFIG_VIRTIO_VSOCKETS"
  ["vmw_vsock_virtio_transport_common"]="CONFIG_VIRTIO_VSOCKETS"

  # ── Virtio ──────────────────────────────────────────────────────────────
  ["virtio"]="CONFIG_VIRTIO"
  ["virtio_ring"]="CONFIG_VIRTIO"
  ["virtio_mmio"]="CONFIG_VIRTIO_MMIO"
  ["virtio_pci"]="CONFIG_VIRTIO_PCI"
  ["virtio_net"]="CONFIG_VIRTIO_NET"
  ["virtio_blk"]="CONFIG_VIRTIO_BLK"
  ["virtio_scsi"]="CONFIG_SCSI_VIRTIO"
  ["virtio_input"]="CONFIG_VIRTIO_INPUT"
  ["virtio_console"]="CONFIG_VIRTIO_CONSOLE"
  ["virtiofs"]="CONFIG_VIRTIO_FS"
  ["blk-mq-virtio"]="CONFIG_VIRTIO_BLK"

  # ── MMC / SDHCI ─────────────────────────────────────────────────────────
  ["mmc_core"]="CONFIG_MMC"
  ["mmc_block"]="CONFIG_MMC_BLOCK"
  ["sdhci"]="CONFIG_MMC_SDHCI"
  ["sdhci-acpi"]="CONFIG_MMC_SDHCI_ACPI"
  ["sdhci-pci"]="CONFIG_MMC_SDHCI_PCI"
  ["sdhci-pci-data"]="CONFIG_MMC_SDHCI_PCI"
  ["sdhci-pltfm"]="CONFIG_MMC_SDHCI_PLTFM"
  ["sdhci-xenon-driver"]="CONFIG_MMC_SDHCI_XENON"
  ["rtsx_pci"]="CONFIG_MFD_RTSX_PCI"
  ["rtsx_pci_sdmmc"]="CONFIG_MMC_REALTEK_PCI"
  ["rtsx_usb"]="CONFIG_MFD_RTSX_USB"
  ["rtsx_usb_sdmmc"]="CONFIG_MMC_REALTEK_USB"
  ["mtk-sd"]="CONFIG_MMC_MTK"
  ["vub300"]="CONFIG_MMC_VUB300"
  ["ushc"]="CONFIG_MMC_USHC"
  ["via-sdmmc"]="CONFIG_MMC_VIA_SDMMC"
  ["cqhci"]="CONFIG_MMC_CQHCI"
  ["alcor_pci"]="CONFIG_MMC_ALCOR"
  ["sdio_uart"]="CONFIG_SDIO_UART"

  # ── Netfilter / iptables ─────────────────────────────────────────────────
  ["ip_tables"]="CONFIG_IP_NF_IPTABLES"
  ["ip6_tables"]="CONFIG_IP6_NF_IPTABLES"
  ["x_tables"]="CONFIG_NETFILTER_XTABLES"
  ["nf_conntrack"]="CONFIG_NF_CONNTRACK"
  ["nf_nat"]="CONFIG_NF_NAT"
  ["nf_defrag_ipv4"]="CONFIG_NF_DEFRAG_IPV4"
  ["nf_defrag_ipv6"]="CONFIG_NF_DEFRAG_IPV6"
  ["nf_log_common"]="CONFIG_NF_LOG_COMMON"
  ["nf_log_ipv4"]="CONFIG_NF_LOG_IPV4"
  ["nf_log_ipv6"]="CONFIG_NF_LOG_IPV6"
  ["iptable_filter"]="CONFIG_IP_NF_FILTER"
  ["iptable_mangle"]="CONFIG_IP_NF_MANGLE"
  ["iptable_nat"]="CONFIG_IP_NF_NAT"
  ["ip6table_filter"]="CONFIG_IP6_NF_FILTER"
  ["ip6table_mangle"]="CONFIG_IP6_NF_MANGLE"
  ["ip6table_nat"]="CONFIG_IP6_NF_NAT"
  ["ip6table_raw"]="CONFIG_IP6_NF_RAW"
  ["nfnetlink"]="CONFIG_NETFILTER_NETLINK"
  ["nfnetlink_acct"]="CONFIG_NETFILTER_NETLINK_ACCT"
  ["nfnetlink_queue"]="CONFIG_NETFILTER_NETLINK_QUEUE"
  ["ipt_MASQUERADE"]="CONFIG_IP_NF_TARGET_MASQUERADE"
  ["xt_MASQUERADE"]="CONFIG_NETFILTER_XT_TARGET_MASQUERADE"
  ["xt_addrtype"]="CONFIG_NETFILTER_XT_MATCH_ADDRTYPE"
  ["xt_comment"]="CONFIG_NETFILTER_XT_MATCH_COMMENT"
  ["xt_connmark"]="CONFIG_NETFILTER_XT_MATCH_CONNMARK"
  ["xt_conntrack"]="CONFIG_NETFILTER_XT_MATCH_CONNTRACK"
  ["xt_iprange"]="CONFIG_NETFILTER_XT_MATCH_IPRANGE"
  ["xt_ipvs"]="CONFIG_NETFILTER_XT_MATCH_IPVS"
  ["xt_l2tp"]="CONFIG_NETFILTER_XT_MATCH_L2TP"
  ["xt_limit"]="CONFIG_NETFILTER_XT_MATCH_LIMIT"
  ["xt_LOG"]="CONFIG_NETFILTER_XT_TARGET_LOG"
  ["xt_mac"]="CONFIG_NETFILTER_XT_MATCH_MAC"
  ["xt_mark"]="CONFIG_NETFILTER_XT_MARK"
  ["xt_multiport"]="CONFIG_NETFILTER_XT_MATCH_MULTIPORT"
  ["xt_nat"]="CONFIG_NETFILTER_XT_NAT"
  ["xt_NFQUEUE"]="CONFIG_NETFILTER_XT_TARGET_NFQUEUE"
  ["xt_owner"]="CONFIG_NETFILTER_XT_MATCH_OWNER"
  ["xt_policy"]="CONFIG_NETFILTER_XT_MATCH_POLICY"
  ["xt_recent"]="CONFIG_NETFILTER_XT_MATCH_RECENT"
  ["xt_REDIRECT"]="CONFIG_NETFILTER_XT_TARGET_REDIRECT"
  ["xt_set"]="CONFIG_NETFILTER_XT_SET"
  ["xt_socket"]="CONFIG_NETFILTER_XT_MATCH_SOCKET"
  ["xt_state"]="CONFIG_NETFILTER_XT_MATCH_STATE"
  ["xt_string"]="CONFIG_NETFILTER_XT_MATCH_STRING"
  ["xt_TCPMSS"]="CONFIG_NETFILTER_XT_TARGET_TCPMSS"
  ["xt_tcpudp"]="CONFIG_NETFILTER_XT_MATCH_TCPUDP"
  ["xt_TPROXY"]="CONFIG_NETFILTER_XT_TARGET_TPROXY"
  ["ip_vs"]="CONFIG_IP_VS"
  ["ip_vs_rr"]="CONFIG_IP_VS_RR"
  ["ip_set"]="CONFIG_IP_SET"
  ["ip_set_hash_ip"]="CONFIG_IP_SET_HASH_IP"
  ["ip_set_hash_ipport"]="CONFIG_IP_SET_HASH_IPPORT"
  ["ip_set_hash_ipportnet"]="CONFIG_IP_SET_HASH_IPPORTNET"
  ["ip_set_hash_net"]="CONFIG_IP_SET_HASH_NET"
  ["nf_conntrack_ipv4"]="CONFIG_NF_CONNTRACK_IPV4"
  ["nf_conntrack_ipv6"]="CONFIG_NF_CONNTRACK_IPV6"
  ["nf_conntrack_pptp"]="CONFIG_NF_CT_PROTO_GRE"
  ["nf_conntrack_proto_gre"]="CONFIG_NF_CT_PROTO_GRE"
  ["nf_nat_ipv4"]="CONFIG_NF_NAT_IPV4"
  ["nf_nat_masquerade_ipv4"]="CONFIG_NF_NAT_MASQUERADE_IPV4"
  ["nf_nat_pptp"]="CONFIG_NF_NAT_PPTP"
  ["nf_nat_proto_gre"]="CONFIG_NF_NAT_PROTO_GRE"
  ["nf_nat_redirect"]="CONFIG_NF_NAT_REDIRECT"
  ["nf_socket_ipv4"]="CONFIG_NF_SOCKET_IPV4"
  ["nf_socket_ipv6"]="CONFIG_NF_SOCKET_IPV6"
  ["nf_tproxy_ipv4"]="CONFIG_NF_TPROXY_IPV4"
  ["nf_tproxy_ipv6"]="CONFIG_NF_TPROXY_IPV6"

  # ── TC / QDisc ──────────────────────────────────────────────────────────
  ["sch_htb"]="CONFIG_NET_SCH_HTB"
  ["sch_codel"]="CONFIG_NET_SCH_CODEL"
  ["sch_fq"]="CONFIG_NET_SCH_FQ"
  ["sch_fq_codel"]="CONFIG_NET_SCH_FQ_CODEL"
  ["sch_fq_pie"]="CONFIG_NET_SCH_FQ_PIE"
  ["sch_netem"]="CONFIG_NET_SCH_NETEM"
  ["sch_sfq"]="CONFIG_NET_SCH_SFQ"
  ["sch_mqprio"]="CONFIG_NET_SCH_MQPRIO"
  ["sch_multiq"]="CONFIG_NET_SCH_MULTIQ"
  ["sch_pie"]="CONFIG_NET_SCH_PIE"
  ["sch_cake"]="CONFIG_NET_SCH_CAKE"
  ["cls_fw"]="CONFIG_NET_CLS_FW"
  ["cls_u32"]="CONFIG_NET_CLS_U32"

  # ── GPIO ────────────────────────────────────────────────────────────────
  ["gpio-it87"]="CONFIG_GPIO_IT87"
  ["gpio-sch"]="CONFIG_GPIO_SCH"
  ["gpio-f7188x"]="CONFIG_GPIO_F7188X"
  ["gpio-sch311x"]="CONFIG_GPIO_SCH311X"
  ["gpio-vx855"]="CONFIG_GPIO_VX855"
  ["gpio-beeper"]="CONFIG_GPIO_BEEPER"
  ["gpio-charger"]="CONFIG_GPIO_CHARGER"

  # ── PHY (platform) ──────────────────────────────────────────────────────
  ["phy-mt65xx-usb3"]="CONFIG_PHY_MTK_TPHY"
  ["phy-intel-lgm-emmc"]="CONFIG_PHY_INTEL_LGM_EMMC"
  ["phy-lgm-usb"]="CONFIG_PHY_INTEL_LGM_USB"

  # ── KCS / BMC ────────────────────────────────────────────────────────────
  ["kcs_bmc"]="CONFIG_KCS_BMC"
  ["kcs_bmc_npcm7xx"]="CONFIG_KCS_BMC_NPCM7XX"
  ["kcs_bmc_aspeed"]="CONFIG_KCS_BMC_ASPEED"
  ["bt-bmc"]="CONFIG_BT_BMC"

  # ── Network misc ─────────────────────────────────────────────────────────
  ["page_pool"]="CONFIG_PAGE_POOL"
  ["devlink"]="CONFIG_NET_DEVLINK"

  # ── IPMI ────────────────────────────────────────────────────────────────
  ["ipmi_msghandler"]="CONFIG_IPMI_HANDLER"
  ["ipmi_si"]="CONFIG_IPMI_SI"
  ["ipmi_devintf"]="CONFIG_IPMI_DEVICE_INTERFACE"
  ["ipmi_poweroff"]="CONFIG_IPMI_POWEROFF"
  ["ipmi_ssif"]="CONFIG_IPMI_SSIF"
  ["ipmi_plat_data"]="CONFIG_IPMI_PLAT_DATA"

  # ── Sound / ALSA ────────────────────────────────────────────────────────
  ["snd"]="CONFIG_SND"
  ["soundcore"]="CONFIG_SOUND"
  ["snd-hda-core"]="CONFIG_SND_HDA"
  ["snd-hda-codec"]="CONFIG_SND_HDA_CODEC"
  ["snd-hda-intel"]="CONFIG_SND_HDA_INTEL"
  ["snd-hda-ext-core"]="CONFIG_SND_HDA_EXT_CORE"
  ["snd-intel-dspcfg"]="CONFIG_SND_INTEL_DSP_CONFIG"
  ["snd-hda-codec-generic"]="CONFIG_SND_HDA_CODEC_GENERIC"
  ["snd-hda-codec-realtek"]="CONFIG_SND_HDA_CODEC_REALTEK"
  ["snd-hda-codec-analog"]="CONFIG_SND_HDA_CODEC_ANALOG"
  ["snd-hda-codec-via"]="CONFIG_SND_HDA_CODEC_VIA"
  ["snd-hda-codec-hdmi"]="CONFIG_SND_HDA_CODEC_HDMI"
  ["snd-hda-codec-cirrus"]="CONFIG_SND_HDA_CODEC_CIRRUS"
  ["snd-hda-codec-ca0110"]="CONFIG_SND_HDA_CODEC_CA0110"
  ["snd-hda-codec-ca0132"]="CONFIG_SND_HDA_CODEC_CA0132"
  ["snd-hda-codec-cmedia"]="CONFIG_SND_HDA_CODEC_CMEDIA"
  ["snd-hda-codec-conexant"]="CONFIG_SND_HDA_CODEC_CONEXANT"
  ["snd-hda-codec-idt"]="CONFIG_SND_HDA_CODEC_IDT"
  ["snd-hda-codec-si3054"]="CONFIG_SND_HDA_CODEC_SI3054"
  ["snd-pcm"]="CONFIG_SND_PCM"
  ["snd-pcm-oss"]="CONFIG_SND_PCM_OSS"
  ["snd-pcm-dmaengine"]="CONFIG_SND_DMAENGINE_PCM"
  ["snd-timer"]="CONFIG_SND_TIMER"
  ["snd-rawmidi"]="CONFIG_SND_RAWMIDI"
  ["snd-hwdep"]="CONFIG_SND_HWDEP"
  ["snd-compress"]="CONFIG_SND_COMPRESS_OFFLOAD"
  ["snd-mixer-oss"]="CONFIG_SND_MIXER_OSS"
  ["snd-seq"]="CONFIG_SND_SEQUENCER"
  ["snd-seq-device"]="CONFIG_SND_SEQ_DEVICE"
  ["snd-seq-midi"]="CONFIG_SND_SEQ_MIDI"
  ["snd-seq-midi-emul"]="CONFIG_SND_SEQ_MIDI_EMUL"
  ["snd-seq-midi-event"]="CONFIG_SND_SEQ_MIDI_EVENT"
  ["snd-seq-virmidi"]="CONFIG_SND_SEQ_VIRMIDI"
  ["snd-usb-audio"]="CONFIG_SND_USB_AUDIO"
  ["snd-usb-hiface"]="CONFIG_SND_USB_HIFACE"
  ["snd-usbmidi-lib"]="CONFIG_SND_USB_AUDIO"
  ["snd-ac97-codec"]="CONFIG_SND_AC97_CODEC"
  ["ac97_bus"]="CONFIG_AC97_BUS"
  ["snd-emu10k1"]="CONFIG_SND_EMU10K1"
  ["snd-emu10k1x"]="CONFIG_SND_EMU10K1X"
  ["snd-emu10k1-synth"]="CONFIG_SND_SEQUENCER_OSS"
  ["snd-emux-synth"]="CONFIG_SND_EMU8000_OSS"
  ["snd-util-mem"]="CONFIG_SND_EMU10K1"
  ["snd-soc-core"]="CONFIG_SND_SOC"
  ["snd-soc-ac97"]="CONFIG_SND_SOC_AC97_BUS"
  ["snd-soc-acpi"]="CONFIG_SND_SOC_ACPI"
  ["sound_firmware"]="CONFIG_SND_AC97_CODEC"

  # ── Framebuffer / GPU ───────────────────────────────────────────────────
  ["fb"]="CONFIG_FB"
  ["fbdev"]="CONFIG_FB"
  ["fbcon"]="CONFIG_FRAMEBUFFER_CONSOLE"
  ["font"]="CONFIG_FONT_SUPPORT"
  ["cfbcopyarea"]="CONFIG_FB_CFB_COPYAREA"
  ["cfbfillrect"]="CONFIG_FB_CFB_FILLRECT"
  ["cfbimgblt"]="CONFIG_FB_CFB_IMAGEBLT"
  ["syscopyarea"]="CONFIG_FB_SYS_COPYAREA"
  ["sysfillrect"]="CONFIG_FB_SYS_FILLRECT"
  ["sysimgblt"]="CONFIG_FB_SYS_IMAGEBLIT"
  ["fb_sys_fops"]="CONFIG_FB_SYS_FOPS"
  ["bitblit"]="CONFIG_FRAMEBUFFER_CONSOLE"
  ["softcursor"]="CONFIG_FB"
  ["vesafb"]="CONFIG_FB_VESA"
  ["vga16fb"]="CONFIG_FB_VGA16"
  ["efifb"]="CONFIG_FB_EFI"
  ["vgastate"]="CONFIG_VGASTATE"
  ["backlight"]="CONFIG_BACKLIGHT_CLASS_DEVICE"
  ["drm"]="CONFIG_DRM"
  ["drm_kms_helper"]="CONFIG_DRM_KMS_HELPER"
  ["drm_panel_orientation_quirks"]="CONFIG_DRM_PANEL_ORIENTATION_QUIRKS"
  ["drm_buddy"]="CONFIG_DRM_BUDDY"
  ["drm_display_helper"]="CONFIG_DRM_DISPLAY_HELPER"
  ["drm_mipi_dsi"]="CONFIG_DRM_MIPI_DSI"
  ["i915"]="CONFIG_DRM_I915"
  ["i915-compat"]="CONFIG_DRM_I915"
  ["intel-gtt"]="CONFIG_DRM_I915"
  ["ttm"]="CONFIG_DRM_TTM"
  ["virtio-gpu"]="CONFIG_DRM_VIRTIO_GPU"
  ["virtio_dma_buf"]="CONFIG_DRM_VIRTIO_GPU"
  ["bochs-drm"]="CONFIG_DRM_BOCHS"
  ["amdgpu"]="CONFIG_DRM_AMDGPU"
  ["gpu-sched"]="CONFIG_DRM_SCHED"
  ["drm_ttm_helper"]="CONFIG_DRM_TTM_HELPER"
  ["drm_vram_helper"]="CONFIG_DRM_VRAM_HELPER"
  ["arcpgu"]="CONFIG_DRM_ARCPGU"
  ["udl"]="CONFIG_DRM_UDL"
  ["dimlib"]="CONFIG_DRM_KMS_HELPER"
  ["video"]="CONFIG_ACPI_VIDEO"

  # ── CPU freq / ACPI ─────────────────────────────────────────────────────
  ["acpi-cpufreq"]="CONFIG_X86_ACPI_CPUFREQ"
  ["cpufreq_conservative"]="CONFIG_CPU_FREQ_GOV_CONSERVATIVE"
  ["cpufreq_governor"]="CONFIG_CPU_FREQ_GOV_COMMON"
  ["cpufreq_ondemand"]="CONFIG_CPU_FREQ_GOV_ONDEMAND"
  ["cpufreq_performance"]="CONFIG_CPU_FREQ_GOV_PERFORMANCE"
  ["cpufreq_powersave"]="CONFIG_CPU_FREQ_GOV_POWERSAVE"
  ["cpufreq_stats"]="CONFIG_CPU_FREQ_STAT"
  ["cpufreq_userspace"]="CONFIG_CPU_FREQ_GOV_USERSPACE"
  ["processor"]="CONFIG_ACPI_PROCESSOR"
  ["button"]="CONFIG_ACPI_BUTTON"
  ["thermal"]="CONFIG_THERMAL"
  ["intel_idle"]="CONFIG_INTEL_IDLE"
  ["coretemp"]="CONFIG_SENSORS_CORETEMP"

  # ── Hwmon sensors ────────────────────────────────────────────────────────
  ["hwmon-vid"]="CONFIG_HWMON_VID"
  ["dme1737"]="CONFIG_SENSORS_DME1737"
  ["nct6775"]="CONFIG_SENSORS_NCT6775"
  ["nct6683"]="CONFIG_SENSORS_NCT6683"
  ["it87"]="CONFIG_SENSORS_IT87"
  ["jc42"]="CONFIG_SENSORS_JC42"
  ["f71882fg"]="CONFIG_SENSORS_F71882FG"
  ["f75375s"]="CONFIG_SENSORS_F75375S"
  ["adm1021"]="CONFIG_SENSORS_ADM1021"
  ["adm1031"]="CONFIG_SENSORS_ADM1031"
  ["adm9240"]="CONFIG_SENSORS_ADM9240"
  ["adt7470"]="CONFIG_SENSORS_ADT7470"
  ["adt7475"]="CONFIG_SENSORS_ADT7475"
  ["lm75"]="CONFIG_SENSORS_LM75"
  ["lm78"]="CONFIG_SENSORS_LM78"
  ["lm90"]="CONFIG_SENSORS_LM90"
  ["lm95245"]="CONFIG_SENSORS_LM95245"
  ["w83781d"]="CONFIG_SENSORS_W83781D"
  ["w83793"]="CONFIG_SENSORS_W83793"
  ["pmbus"]="CONFIG_PMBUS"
  ["pmbus_core"]="CONFIG_PMBUS"
  ["drivetemp"]="CONFIG_SENSORS_DRIVETEMP"

  # ── RTC ─────────────────────────────────────────────────────────────────
  ["rtc-cmos"]="CONFIG_RTC_DRV_CMOS"

  # ── USB Serial ──────────────────────────────────────────────────────────
  ["usbserial"]="CONFIG_USB_SERIAL"
  ["ftdi_sio"]="CONFIG_USB_SERIAL_FTDI_SIO"
  ["cp210x"]="CONFIG_USB_SERIAL_CP210X"
  ["ch341"]="CONFIG_USB_SERIAL_CH341"
  ["pl2303"]="CONFIG_USB_SERIAL_PL2303"
  ["wch"]="CONFIG_USB_SERIAL_WCH"
  ["wch_pre"]="CONFIG_USB_SERIAL_WCH"

  # ── USB Host controllers ─────────────────────────────────────────────────
  ["ehci-hcd"]="CONFIG_USB_EHCI_HCD"
  ["ehci-pci"]="CONFIG_USB_EHCI_PCI"
  ["uhci-hcd"]="CONFIG_USB_UHCI_HCD"
  ["xhci-hcd"]="CONFIG_USB_XHCI_HCD"

  # ── Input ───────────────────────────────────────────────────────────────
  ["evdev"]="CONFIG_INPUT_EVDEV"
  ["evbug"]="CONFIG_INPUT_EVBUG"
  ["atkbd"]="CONFIG_KEYBOARD_ATKBD"
  ["i8042"]="CONFIG_SERIO_I8042"
  ["pcspkr"]="CONFIG_INPUT_PCSPKR"
  ["pcspeaker"]="CONFIG_INPUT_PCSPKR"

  # ── Storage misc ─────────────────────────────────────────────────────────
  ["pktcdvd"]="CONFIG_CDROM_PKTCDVD"
  ["ide-core"]="CONFIG_IDE"
  ["ide-gd_mod"]="CONFIG_BLK_DEV_IDEDISK"
  ["ide-pci-generic"]="CONFIG_BLK_DEV_GENERIC"
  ["ntfs"]="CONFIG_NTFS_FS"
  ["exfat"]="CONFIG_EXFAT_FS"
  ["fuse"]="CONFIG_FUSE_FS"

  # ── 9P ──────────────────────────────────────────────────────────────────
  ["9p"]="CONFIG_9P_FS"
  ["9pnet"]="CONFIG_NET_9P"
  ["9pnet_virtio"]="CONFIG_NET_9P_VIRTIO"

  # ── DVB / Media ──────────────────────────────────────────────────────────
  ["dvb-core"]="CONFIG_DVB_CORE"
  ["dvb-usb"]="CONFIG_DVB_USB"
  ["dvb-usb-dib0700"]="CONFIG_DVB_USB_DIB0700"
  ["dvb-usb-dvbsky"]="CONFIG_DVB_USB_DVBSKY"
  ["dvb_usb_v2"]="CONFIG_DVB_USB_V2"
  ["rc-core"]="CONFIG_RC_CORE"
  ["m88ds3103"]="CONFIG_DVB_M88DS3103"
  ["si2157"]="CONFIG_MEDIA_TUNER_SI2157"
  ["si2168"]="CONFIG_DVB_SI2168"
  ["sp2"]="CONFIG_DVB_SP2"
  ["ts2020"]="CONFIG_DVB_TS2020"
  ["eeprom_93cx6"]="CONFIG_EEPROM_93CX6"

  # ── TCP congestion ───────────────────────────────────────────────────────
  ["tcp_bic"]="CONFIG_TCP_CONG_BIC"
  ["tcp_cdg"]="CONFIG_TCP_CONG_CDG"
  ["tcp_dctcp"]="CONFIG_TCP_CONG_DCTCP"
  ["tcp_highspeed"]="CONFIG_TCP_CONG_HIGHSPEED"
  ["tcp_htcp"]="CONFIG_TCP_CONG_HTCP"
  ["tcp_hybla"]="CONFIG_TCP_CONG_HYBLA"
  ["tcp_illinois"]="CONFIG_TCP_CONG_ILLINOIS"
  ["tcp_lp"]="CONFIG_TCP_CONG_LP"
  ["tcp_scalable"]="CONFIG_TCP_CONG_SCALABLE"
  ["tcp_vegas"]="CONFIG_TCP_CONG_VEGAS"
  ["tcp_veno"]="CONFIG_TCP_CONG_VENO"
  ["tcp_westwood"]="CONFIG_TCP_CONG_WESTWOOD"
  ["tcp_yeah"]="CONFIG_TCP_CONG_YEAH"
  ["tcp_bbr"]="CONFIG_TCP_CONG_BBR"
  ["tcp_nv"]="CONFIG_TCP_CONG_NV"

  # ── Misc platform / buses ────────────────────────────────────────────────
  ["cn"]="CONFIG_CONNECTOR"
  ["llc"]="CONFIG_LLC"
  ["stp"]="CONFIG_STP"
  ["p8022"]="CONFIG_P8022"
  ["psnap"]="CONFIG_PSNAP"
  ["ipv6"]="CONFIG_IPV6"
  ["auxiliary"]="CONFIG_AUXILIARY_BUS"
  ["iosf_mbi"]="CONFIG_IOSF_MBI"
  ["intel-lpss"]="CONFIG_MFD_INTEL_LPSS"
  ["hpilo"]="CONFIG_HP_ILO"
  ["hvc_console"]="CONFIG_HVC_DRIVER"
  ["efivarfs"]="CONFIG_EFIVAR_FS"
  ["regmap-i2c"]="CONFIG_REGMAP_I2C"
  ["regmap-mmio"]="CONFIG_REGMAP_MMIO"
  ["i2c-algo-bit"]="CONFIG_I2C_ALGOBIT"
  ["i2c-i801"]="CONFIG_I2C_I801"
  ["i2c-smbus"]="CONFIG_I2C_SMBUS"
  ["power_supply"]="CONFIG_POWER_SUPPLY"
  ["pwm-fan"]="CONFIG_SENSORS_PWM_FAN"
  ["typec"]="CONFIG_TYPEC"
  ["tcpm"]="CONFIG_TYPEC_TCPM"
  ["fusb302"]="CONFIG_TYPEC_FUSB302"
  ["hd3ss3220"]="CONFIG_TYPEC_HD3SS3220"
  ["thunderbolt"]="CONFIG_THUNDERBOLT"
  ["thunderbolt-net"]="CONFIG_THUNDERBOLT_NET"
  ["tps65217"]="CONFIG_MFD_TPS65217"
  ["libfc"]="CONFIG_LIBFC"
  ["textsearch"]="CONFIG_TEXTSEARCH"
  ["ts_bm"]="CONFIG_TEXTSEARCH_BM"
  ["crc-ccitt"]="CONFIG_CRC_CCITT"
  ["crc-itu-t"]="CONFIG_CRC_ITU_T"
  ["crc8"]="CONFIG_CRC8"
  ["libarc4"]="CONFIG_CRYPTO_LIB_ARC4"
  ["libdes"]="CONFIG_CRYPTO_DES"
  ["libsha256"]="CONFIG_CRYPTO_SHA256"
  ["xz_dec"]="CONFIG_XZ_DEC"
  ["dmabuf"]="CONFIG_DMA_SHARED_BUFFER"
  ["check_signature"]="CONFIG_HAS_IOMEM"

  # ── Out-of-tree / Android IPC ────────────────────────────────────────────
  ["acpi_call"]="CONFIG_ACPI_CALL"
  ["ashmem_linux"]="CONFIG_ASHMEM"
  ["binder_linux"]="CONFIG_ANDROID_BINDER_IPC"
  ["tty0tty"]="CONFIG_TTY0TTY"
  ["gasket"]="CONFIG_STAGING_GASKET_FRAMEWORK"
  ["apex"]="CONFIG_STAGING_APEX_DRIVER"

  # ── MOVBE emulator (compiled separately via movbe/ dir) ─────────────────
  ["movbe_emulator"]="_SKIP_MOVBE"

  # ── Synology proprietary (pre-built, skip compilation) ───────────────────
  ["synobios"]="_SKIP"
  ["aic_load_fw"]="_SKIP"
  ["syno_ahci_reg_read_test"]="_SKIP"
  ["syno_hddpwrctl_test"]="_SKIP"
  ["syno_sata_signal_check"]="_SKIP"
  ["syno_sata_signal_test"]="_SKIP"
  ["syno_jmb585_update_spi"]="_SKIP"

  # ── Out-of-tree vendor blobs (no upstream Kconfig) ───────────────────────
  ["btcoexist"]="_SKIP"          # Realtek BT coexistence, bundled with rtlwifi OOT
  ["etxhci-hcd"]="_SKIP"         # Synology custom XHCI host controller
  ["mlx_compat"]="_SKIP"         # Mellanox OFED compatibility shim, OOT
  ["i2c-asm2824"]="_SKIP"        # ASMedia proprietary I2C
)

# ---------------------------------------------------------------------------
# Lookup CONFIG symbol for a .ko basename
# ---------------------------------------------------------------------------
ko_to_config() {
  local ko="${1//-/_}"   # normalise dashes to underscores for array lookup first
  local val="${KO_TO_CONFIG[$1]}"  # try original name
  [ -z "$val" ] && val="${KO_TO_CONFIG[$ko]}"  # try normalised name
  echo "${val}"
}

# ---------------------------------------------------------------------------
# Collect all .ko files for a platform from its thirdparty directories
# (all toolkit_ver variants for the same platform+kver are merged)
# ---------------------------------------------------------------------------
collect_ko_list() {
  local platform="$1"
  local kver="$2"
  local -n _result=$3   # nameref to result array

  # Find all thirdparty dirs matching platform-*-kver
  local dirs=()
  while IFS= read -r -d '' d; do
    dirs+=("$d")
  done < <(find "${THIRDPARTY_DIR}" -maxdepth 1 -type d -name "${platform}-*-${kver}" -print0 2>/dev/null)

  if [[ ${#dirs[@]} -eq 0 ]]; then
    log_warn "No thirdparty directory found for ${platform}-*-${kver}"
    return
  fi

  declare -A seen
  for dir in "${dirs[@]}"; do
    while IFS= read -r f; do
      local bn
      bn="$(basename "${f}" .ko)"
      [ -z "${seen[$bn]+x}" ] && { _result+=("$bn"); seen[$bn]=1; }
    done < <(find "${dir}" -maxdepth 1 -name "*.ko" 2>/dev/null)
  done
}

# ---------------------------------------------------------------------------
# Generate one defines file
# ---------------------------------------------------------------------------
generate_defines() {
  local platform="$1"
  local kver="$2"
  local dir_prefix="${SCRIPT_DIR}/${kver:0:1}.x"
  local out_file="${dir_prefix}/defines.${platform}"

  if [ ! -d "${dir_prefix}" ]; then
    log_warn "Skipping ${platform} – source dir ${dir_prefix} not found"
    return
  fi

  local -a ko_list=()
  collect_ko_list "${platform}" "${kver}" ko_list

  if [[ ${#ko_list[@]} -eq 0 ]]; then
    log_warn "No .ko files found for ${platform}/${kver}, skipping"
    return
  fi

  log_info "Generating ${out_file} (${#ko_list[@]} modules)"

  # Collect unique CONFIG entries (deduplicated, sorted)
  declare -A config_set
  local -a skipped=()

  for ko in "${ko_list[@]}"; do
    local cfg
    cfg="$(ko_to_config "${ko}")"

    if [ -z "$cfg" ]; then
      log_debug "  [UNKNOWN] ${ko}.ko – no CONFIG mapping, skipping"
      skipped+=("${ko}")
      continue
    fi

    case "$cfg" in
      _SKIP*)
        log_debug "  [SKIP] ${ko}.ko"
        ;;
      *)
        config_set["$cfg"]=1
        ;;
    esac
  done

  # Write the file
  {
    local ident="${PLATFORM_IDENT[$platform]:-CONFIG_$(echo "${platform}" | tr '[:lower:]' '[:upper:]')=y}"
    echo "# Auto-generated by gen-defines.sh — $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "# Platform: ${platform}  Kernel: ${kver}"
    echo "# Source: thirdparty module list"
    echo ""
    echo "${ident}"
    echo ""

    # Group by category (comment headers from CONFIG prefix patterns)
    echo "# ATA / SATA"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(ATA|SATA|PATA|PDC)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Block / SCSI / RAID"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(SCSI|BLK_DEV|RAID|MEGARAID|FUSION|CHR_DEV|ISCSI|LIBISCSI|CDROM)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Network – wired"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(E1000|IGB|IGC|IXGB|IXGBE|I40E|IAVF|ICE|B44|BNX|BNXT|TIGON|TG3|MLX|MLXSW|SKY|SKGE|JME|VIA_RHINE|VIA_VELOC|VMXNET|AQTION|ALX|ATL|BE2NET|CHELSIO|QED|QEDE|QLA|QLCNIC|QLGE|NETXEN|SFC|8139|8390|NE2K|R8|REALTEK|BROADCOM|SSB|BCM|HINIC|FORCEDETH|WIREGUARD|YT6801|NET_VENDOR_TEHUTI)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Network – USB"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(USB_(NET|LAN|SMSC|AQC|RTL|USBNET)|PHYLIB|MII|MDIO|NET_FAILOVER|GENERIC_PHY|FIXED_PHY)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Network – misc / PHY"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(PHYLIB|MII|MDIO|NET_FAILOVER|GENERIC_PHY|FIXED_PHY|OF_MDIO|LLC|STP|P8022|PSNAP|CONNECTOR|IPV6|NET_9P|9P_FS|AUXILIARY_BUS)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Netfilter / iptables"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(IP(6?_NF|_VS|_SET)|NF_|NETFILTER|XT_)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# TC / QDisc"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(NET_SCH|NET_CLS)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# TCP congestion"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_TCP_CONG' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# KVM / Virtualization"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(KVM|IRQ_BYPASS|VFIO|VMWARE|VSOCKETS|VIRTIO)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# MMC / SDHCI"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(MMC|SDIO|MFD_RTSX)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# IPMI"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_IPMI' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# InfiniBand / RDMA"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(INFINIBAND|RDMA)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Sound / ALSA"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(SND|SOUND|AC97)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Framebuffer / GPU / DRM"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(FB|DRM|FRAMEBUFFER|FONT|VGASTATE|BACKLIGHT)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# CPU freq / ACPI / HWMON"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(X86_ACPI_CPUFREQ|CPU_FREQ|ACPI|THERMAL|INTEL_IDLE|HWMON|SENSORS|RTC|PMBUS)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# USB host / serial / input"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | grep -E '^CONFIG_(USB_(EHCI|UHCI|XHCI|SERIAL)|INPUT|KEYBOARD|SERIO)' | sort); do
      echo "${cfg}=m"
    done
    echo ""

    echo "# Misc"
    for cfg in $(echo "${!config_set[@]}" | tr ' ' '\n' | \
        grep -vE '^CONFIG_(ATA|SATA|PATA|PDC|SCSI|BLK_DEV|RAID|MEGARAID|FUSION|CHR_DEV|ISCSI|LIBISCSI|CDROM|E1000|IGB|IGC|IXGB|IXGBE|I40E|IAVF|ICE|B44|BNX|BNXT|TIGON|TG3|MLX|MLXSW|SKY|SKGE|JME|VIA_RHINE|VIA_VELOC|VMXNET|AQTION|ALX|ATL|BE2NET|CHELSIO|QED|QEDE|QLA|QLCNIC|QLGE|NETXEN|SFC|8139|8390|NE2K|R8|REALTEK|BROADCOM|SSB|BCM|HINIC|FORCEDETH|WIREGUARD|YT6801|NET_VENDOR_TEHUTI|USB_(NET|LAN|SMSC|AQC|RTL|USBNET)|PHYLIB|MII|MDIO|NET_FAILOVER|GENERIC_PHY|FIXED_PHY|OF_MDIO|LLC|STP|P8022|PSNAP|CONNECTOR|IPV6|NET_9P|9P_FS|AUXILIARY_BUS|IP(6?_NF|_VS|_SET)|NF_|NETFILTER|XT_|NET_SCH|NET_CLS|TCP_CONG|KVM|IRQ_BYPASS|VFIO|VMWARE|VSOCKETS|VIRTIO|MMC|SDIO|MFD_RTSX|IPMI|INFINIBAND|RDMA|SND|SOUND|AC97|FB|DRM|FRAMEBUFFER|FONT|VGASTATE|BACKLIGHT|X86_ACPI_CPUFREQ|CPU_FREQ|ACPI|THERMAL|INTEL_IDLE|HWMON|SENSORS|RTC|PMBUS|USB_(EHCI|UHCI|XHCI|SERIAL)|INPUT|KEYBOARD|SERIO)' | sort); do
      echo "${cfg}=m"
    done

  } > "${out_file}"

  if [[ ${#skipped[@]} -gt 0 ]]; then
    log_warn "  ${#skipped[@]} module(s) had no CONFIG mapping (listed below):"
    for s in "${skipped[@]}"; do
      log_warn "    - ${s}.ko"
    done
    log_warn "  Add these to KO_TO_CONFIG in gen-defines.sh if you want them built."
  fi

  log_info "  Written: ${out_file}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
  log_info "=== gen-defines.sh: Generate defines files from thirdparty ==="
  echo ""

  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "PLATFORMS file not found: ${PLATFORMS_FILE}"; exit 1; }
  [ ! -d "${THIRDPARTY_DIR}" ] && { log_error "thirdparty dir not found: ${THIRDPARTY_DIR}"; exit 1; }

  # Collect unique platform+kver pairs (skip duplicates across toolkit versions)
  declare -A seen_platform_kver
  local -a pairs=()

  while read -r PLATFORM KVER TOOLKIT_VER _REST; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    PLATFORM="$(echo "${PLATFORM}" | xargs)"
    KVER="$(echo "${KVER}" | xargs)"
    local key="${PLATFORM}:${KVER}"
    if [ -z "${seen_platform_kver[$key]+x}" ]; then
      seen_platform_kver[$key]=1
      pairs+=("${PLATFORM} ${KVER}")
    fi
  done < "${PLATFORMS_FILE}"

  log_info "Found ${#pairs[@]} unique platform/kernel combinations"
  echo ""

  local generated=0
  local failed=0

  for pair in "${pairs[@]}"; do
    read -r platform kver <<< "${pair}"
    if generate_defines "${platform}" "${kver}"; then
      ((generated++)) || true
    else
      ((failed++)) || true
    fi
  done

  echo ""
  log_info "Done. Generated: ${generated}  Failed: ${failed}"
  echo ""
  log_info "Next step: run build-upstream.sh to compile with fresh kernel source"
}

main "$@"
