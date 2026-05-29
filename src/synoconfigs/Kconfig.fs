menu "File Systems"

menu "Basic"

config SYNO_FS_STAT
	bool "SYNOStat"
	default y
	depends on SYNO_SYSTEM_CALL

config SYNO_FS_XATTR
	bool "syno xattr"
	default y

config SYNO_FS_UNMOUNT
	bool "syno umount dump opened file"
	default y

config SYNO_FS_ARCHIVE_BIT
	bool "syno archive bit"
	default y

config SYNO_FS_ARCHIVE_VERSION
	bool "syno archive version"
	default y

config SYNO_FS_CREATE_TIME
	bool "syno create time"
	default y

config SYNO_FS_EXPORT_SYMBOL_FALLOCATE
	bool "Export symbol: do_fallocate"
	default y

config SYNO_FS_WINACL
	bool ""
	default y
	depends on SYNO_FS_ARCHIVE_BIT

config SYNO_FS_SKIP_RO_NEW_INODE_WARNING
	bool "Skip I_NEW WARN_ON when read-write on crached raid"
	default y

config SYNO_FS_RECVFILE
	bool "Support system call recvfile"
	default y

config SYNO_FS_EXT_SKIP_FSCK_REMINDER
	bool "Skip unused fsck remainder warning on ext2/3/4"
	default y
	depends on EXT2_FS || EXT4_FS

config SYNO_FS_CASELESS_STAT
	bool "Support caseless stat in filesystem "
	default y
	depends on SYNO_SYSTEM_CALL

config SYNO_FS_LOCKER
	bool "Mechanism to lock/unlock data for WORM purpose"
	default y

config SYNO_FS_EXPORT_SYMBOL_LOOKUP_HASH
	bool "Export symbol: lookup_hash"
	default y

config SYNO_FS_REMOVE_RCU_WALK_PATH
	bool "Remove rcu path walk to prevent deadlock"
	default y

config SYNO_FS_NOTIFY
	bool "Support Synotify"
	default y
	depends on FSNOTIFY && ANON_INODES

config SYNO_FS_EXPORT_TRUNCATE
	bool "Export symbol: syno_vfs_truncate"
	default y

config SYNO_FS_FIX_NOTIFY_CHANGE_WARN_ON
	bool "Fix notify_change warn_on on boot"
	default y

config SYNO_FS_RELATIME_PERIOD
	bool "Add mount option to set update period of relatime"
	default y

config SYNO_FS_QUOTA_QUERY
	bool "Support query quota used and limit in vfs operations"
	default y

config SYNO_FS_SYNOBOOT_LOG
	bool "synoboot access log"
	default y

config SYNO_FS_COMPRESSION
	bool "syno compression"
	default y

config SYNO_FS_CACHE_PROTECTION
	tristate "SYNO cache protection support"
	default m
	depends on SYNO_FEATURES && SYNO_NTB && SYNO_NTB_SUPPORT_BRD

config SYNO_FS_SPACE_USAGE
	bool "Support query space usage in vfs operations"
	default y

config SYNO_FS_DEV
	bool "SYNO FS dev support"
	default y
	depends on SWAP && SYNO_FEATURES

config SYNO_FS_RBD_META
	tristate "Syno rbd meta support"
	default y
	depends on SWAP && SYNO_FEATURES

config SYNO_FS_ROOT_PRJQUOTA
	bool "Project quota restricts root usage"
	default y
	depends on QUOTA && SYNO_FEATURES

config SYNO_FS_SHOW_INCOMPAT_SUPP
	bool "Show file system's incompatible support flags"
	default y

config SYNO_FS_SHOW_COMPAT_RO_SUPP
	bool "Show file system's compatible read-only support flags"
	default y

config SYNO_FS_SPLICE_FSNOTIFY
	bool "splice: report related fsnotify events"
	default y

endmenu #Basic

menu "CIFS"
config SYNO_CIFS_REPLACE_NATIVE_OS
	bool "Identify Synology CIFS mount"
	default y

config SYNO_CIFS_TCON_RECONNECT_CODEPAGE_UTF8
	bool "Cifs with UTF8 code page"
	default y

config SYNO_CIFS_INIT_NLINK
	bool "Initialize fattr with cf_nlink=1 in cifs_dir_info_to_fattr"
	default y

config SYNO_CIFS_SPECIAL_CHAR_CONVER
	bool "CIFS convert special char for MAC"
	default y

config SYNO_CIFS_MOUNT_CASELESS
	bool "Caseless remote mount"
	default y

config SYNO_CIFS_NO_SPECIAL_CHAR_LOGON
	bool "STATUS_LOGON_FAILURE when password contains '/'"
	default y
	depends on SYNO_CIFS_SPECIAL_CHAR_CONVER

config SYNO_CIFS_FORCE_UMOUNT
	bool ""
	default y

config SYNO_CIFS_INCREASE_SENDMSG_TIMEOUT
	bool "increase the kernel_sendmsg EAGAIN timeout for more stability"
	default y

config SYNO_CIFS_SMB_OPS
	bool "add vers=syno for switch SMB1~3 from negotiate"
	default y
	depends on SYNO_CIFS_REPLACE_NATIVE_OS

config SYNO_CIFS_COVERITY
	bool "fix cifs coverity"
	default y

config SYNO_CIFS_RECONNECT
	bool "modify cifs reconnect behavior to prevent wait mutex lock cause hung task"
	default y

endmenu #CIFS

menu "FAT"

config SYNO_FAT_DEFAULT_MNT_FLUSH
	bool "Set FAT default mount option 'flush'"
	default y
	depends on FAT_FS

config SYNO_FAT_LOAD_DEF_NLS_IF_FAIL
	bool "Try default nls as codepage setting when default codepage cannot be loaded"
	default y
	depends on FAT_FS

config SYNO_FAT_CREATE_TIME
	bool "FAT syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && FAT_FS

config SYNO_FAT_INODE_KEEP_LIST
	bool "Keep some inode by increase their i_count"
	default y
	depends on FAT_FS

endmenu

menu "EXT3"

config SYNO_EXT3_STAT
	bool "Ext3 SYNOStat"
	default y
	depends on SYNO_FS_STAT && EXT3_FS

config SYNO_EXT3_XATTR
	bool "Ext3 syno xattr"
	default y
	depends on SYNO_FS_XATTR && EXT3_FS

config SYNO_EXT3_ARCHIVE_BIT
	bool "Ext3 syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && EXT3_FS

config SYNO_EXT3_ARCHIVE_VERSION
	bool "Ext3 syno archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && EXT3_FS && SYNO_EXT3_XATTR

config SYNO_EXT3_CREATE_TIME
	bool "Ext3 syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && EXT3_FS

endmenu #EXT3

menu "EXT4"

config SYNO_EXT4_LAZYINIT_INFO
	bool "Export lazyinit progress to sysfs"
	default y
	depends on EXT4_FS

config SYNO_EXT4_LAZYINIT_DYNAMIC_SPEED
	bool "Adjust lazyinit speed dynamically"
	default y
	depends on EXT4_FS

config SYNO_EXT4_LAZYINIT_WAIT_MULT
	int "Number of lazyinit wait multiplier"
	default 2
	depends on EXT4_FS

config SYNO_EXT4_STAT
	bool "Ext4 SYNOStat"
	default y
	depends on SYNO_FS_STAT && EXT4_FS

config SYNO_EXT4_XATTR
	bool "Ext4 syno xattr"
	default y
	depends on SYNO_FS_XATTR && EXT4_FS

config SYNO_EXT4_ARCHIVE_BIT
	bool "Ext4 syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && EXT4_FS

config SYNO_EXT4_ARCHIVE_VERSION
	bool "Ext4 syno archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && EXT4_FS && SYNO_EXT4_XATTR

config SYNO_EXT4_ARCHIVE_VERSION_FIX
	bool "Ext4 syno arhchive version fix"
	default y
	depends on SYNO_EXT4_ARCHIVE_VERSION

config SYNO_EXT4_CREATE_TIME
	bool "Ext4 syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && EXT4_FS

config SYNO_EXT4_CREATE_TIME_BIG_ENDIAN_SWAP
	bool "Ext4 syno create time swap for big endian"
	default y if SYNO_QORIQ || SYNO_MPC8533
	depends on SYNO_EXT4_CREATE_TIME

config SYNO_EXT4_DEFAULT_MNTOPT_JOURNAL_CKSUM
	bool "Ext4 set default mount option journal_checksum"
	default y
	depends on EXT4_FS

config SYNO_EXT4_DEFAULT_MNTOPT_BARRIER_ROOTFS
	bool "Ext4 set default mount option barrier=1 on root fs"
	default y
	depends on EXT4_FS

config SYNO_EXT4_ERROR_REPORT
	bool "Enable ext4 error report mechanism"
	default y
	depends on EXT4_FS

config SYNO_EXT4_INODE_NUM_OVERFLOW_FIX
	bool "Fix ext4 inode number overflow problem on large volume (>64TB)"
	default y
	depends on EXT4_FS && 64BIT

config SYNO_EXT4_WINACL
	bool "Enable ext4 syno acl"
	default y
	depends on EXT4_FS && SYNO_FS_WINACL

config SYNO_EXT4_CASELESS_STAT
	bool "Support caseless stat in ext4"
	default y
	depends on SYNO_FS_CASELESS_STAT && EXT4_FS

config SYNO_EXT4_SKIP_ADD_RESERVED_BLOCKS
	bool "Don't update reserved blocks on resize"
	default y
	depends on EXT4_FS

config SYNO_EXT4_RESIZE_INODE_SIZE_EXTEND
	bool "Increase reserved GDT to break the 16TB boundary of online resize"
	default y
	depends on EXT4_FS

config SYNO_EXT4_PARALLEL_GROUP_DESC_PREFETCH_WHEN_MOUNT
	bool "Add parallel group desc prefetching to enhance mount time."
	default y
	depends on EXT4_FS

config SYNO_EXT4_ADD_RETRY_MECH_FOR_SYMLINK
	bool "Add retry back for symlink to prevant ENOSPC error."
	default y
	depends on EXT4_FS

config SYNO_EXT4_UNUSED_HINT
	bool "FIHINTUNUSED ioctl to send free space information to underly layers"
	default y
	depends on SYNO_MD_UNUSED_HINT
	depends on EXT4_FS

config SYNO_EXT4_METADATA_CSUM_SMOOTH_ERR_HANDLING
	bool "Smooth ext4 checksum err handling to prevent users from losing new volume data"
	default y
	depends on EXT4_FS

config SYNO_EXT4_SYMLINK_IOCTL
	bool "add ioctl to symbolic link"
	default y
	depends on EXT4_FS

config SYNO_EXT4_DISABLE_INODES_COUNT_CHECK
	bool "disable mount time check about inodes count"
	default y
	depends on EXT4_FS

config SYNO_EXT4_CAPABILITY_FLAGS
	bool "synology capability flags"
	default y
	depends on EXT4_FS

config SYNO_EXT4_RBD_META
	bool "Reserve SynoRBD meta in filesystem"
	default y
	depends on EXT4_FS && SYNO_FS_RBD_META && SYNO_EXT4_CAPABILITY_FLAGS

config SYNO_EXT4_SKIP_UNNECESSARY_BARRIER
	bool "Skip blk flush with data!=writeback when sync_fs"
	default y
	depends on EXT4_FS

config SYNO_EXT4_BH_FLAGS_WARNING
	bool "Workaround for incorrect bh flags"
	default y
	depends on EXT4_FS

config SYNO_EXT4_FIX_PADDING_ZERO
	bool "Fix padding zero issues after crash"
	default y
	depends on EXT4_FS

endmenu #EXT4

menu "BTRFS"

config SYNO_BTRFS_PORTING
	bool "Btrfs back porting"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_STAT
	bool "Btrfs SYNOStat"
	default y
	depends on SYNO_FS_STAT && BTRFS_FS

config SYNO_BTRFS_XATTR
	bool "Btrfs syno xattr"
	default y
	depends on SYNO_FS_XATTR && BTRFS_FS

config SYNO_BTRFS_ARCHIVE_BIT
	bool "Btrfs syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && BTRFS_FS && SYNO_BTRFS_XATTR

config SYNO_BTRFS_WINACL
	bool "Enable btrfs syno acl"
	default y
	depends on BTRFS_FS && SYNO_FS_WINACL && !BTRFS_FS_POSIX_ACL

config SYNO_BTRFS_PIN_LOG_ON_DELETE_INODE
	bool "Pin tree-log while unlink to prevent deadlock."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_METADATA_OVERCOMMIT_POLICY
	bool "Change metadata over commit policy to prevent file system crash."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_PAGE_LEAK_WHILE_CLONE_EXTENT_BUFFER
	bool "Fix btrfs memory leak on clone extent buffer."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLONE_CHECK_QUOTA
	bool "Add quota check for IOC_CLONE ioctl command"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_FAST_QGROUP

config SYNO_BTRFS_FREE_EXTENT_MAPS
	bool "Add a machanisim to drop extent map cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLUSTER_ALLOCATION
	bool "Cluster allocation"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CREATE_TIME
	bool "Add syno create time for btrfs"
	default y
	depends on SYNO_FS_CREATE_TIME && SYNO_BTRFS_XATTR && BTRFS_FS

config SYNO_BTRFS_RESIZE_QUERY
	bool "Add a dry-run mode in BTRFS_IOC_RESIZE"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DATA_CORRECTION
	bool "Report btrfs data checksum failure"
	default n
	depends on BTRFS_FS && SYNO_DATA_CORRECTION

config SYNO_BTRFS_SUBVOLUME_HIDE
	bool "Support subvolume hide flag"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ARCHIVE_VERSION
	bool "Support syno archive version for btrfs"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && SYNO_BTRFS_XATTR && BTRFS_FS

config SYNO_BTRFS_CASELESS_STAT
	bool "Add syno caseless stat for btrfs"
	default y
	depends on SYNO_FS_CASELESS_STAT && BTRFS_FS

config SYNO_BTRFS_LOCKER
	bool "Mechanism to lock/unlock data for WORM purpose"
	default y
	depends on BTRFS_FS && SYNO_FS_LOCKER && SYNO_BTRFS_FEATURE_TREE

config SYNO_BTRFS_LOCKER_SNAPSHOT
	bool "Mechanism to lock/unlock read-only snapshot"
	default y
	depends on SYNO_BTRFS_LOCKER

config SYNO_BTRFS_LOCKER_SUBVOLUME_CLOCK
	bool "Support subvolume clock for locker"
	default y
	depends on SYNO_BTRFS_LOCKER

config SYNO_BTRFS_SEND
	bool "Add syno btrfs send"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_FIX_ASYNC_DIRECT_IO_CSUM_FAILED
	bool "Disable btrfs async read on direct io."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SUPPORT_FULLY_CLONE_BETWEEN_CSUM_AND_NOCSUM_DIR
	bool "Fix cp --reflink failed between csum/nocsum"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QGROUP_QUERY
	bool "Add ioctl for btrfs qgroup query"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RENAME_READONLY_SUBVOL
	bool "Fix rename readonly subvol fail"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_USRQUOTA
	bool "Btrfs sub-volume usrquota"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_FAST_QGROUP

config SYNO_BTRFS_REMOVE_UNUSED_QGROUP
	bool "Remove qgroup item when snapshot got deleted"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FAST_QGROUP
	bool "Syno version fast qgroup accounting"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_BACKREF

config SYNO_BTRFS_REVERT_WAIT_OR_COMMIT_SELF_TRANS
	bool "Revert commit: wait or commit self transaction"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BIG_BLOCK_GROUP
	bool "Use big block group to reduce mount time on big volume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REVERT_BIO_COUNT_FOR_DEV_REPLACING
	bool "Fix btrfs hang on btrfs_end_io*"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_CALCULATE_TOTAL_DATA_SIZE
	bool "add btrfs calculate send data size"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_SUBVOL_FLAG
	bool "add btrfs send subvol flag"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_REVERT_DELAYED_DELETE_INODE
	bool "Fix dbench hang on delayed_delete_inode"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_UMOUNT_ERROR_VOLUME
	bool "Fix umount on a error Btrfs volume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DISABLE_CLONE_BETWEEN_COMPR_AND_NOCOMPR_DIR
	bool "Prevent clone files between compress/nocompress share folders"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPR_CTL
	bool "Operate compressed files"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPR_DEFAULT_SETTING
	bool "Apply default setting of syno compression"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_METADATA_RESERVE
	bool "Pre-allocate btrfs metadata chunk with metadata_ratio."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_INCREMENTAL_SEND
	bool "fix btrfs send incremental send"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_NULL_ACCESS_IN_PENDING_SNAPSHOT
	bool "Avoid NULL pointer dereference at create_pending_snapshots"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ADD_LOCK_ON_FLUSH_ORDERED_EXTENT
	bool "Add mutex lock on btrfs ordered extent flush to prevent memory leak."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BLOCK_GROUP_HINT_TREE
	bool "Add a block group hint tree to speedup volume mount."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RESERVE_PROP_SPACE_FOR_COMPRESSION
	bool "Reserve space for compression props on start_transaction."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_TRIM_SYS_CHUNK
	bool "Avoid trim system chunk."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_GLOBAL_RESERVE_MINIMAL_VALUE
	bool "Keep btrfs global reserve more than 256MB if the fs is larger than 10G."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REVERT_FITRIM_UNUSED_CHUNK_SPACE
	bool "Revert commit btrfs: iterate over unused chunk space in FITRIM"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QGROUP_LIMIT
	bool "fix set qgroup limit to not check flag, because compatibility with old version"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SUPPORT_PAUSE_RESUME
	bool "add btrfs send support pause/resume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_TREE_LOG_RECOVER_FIX
	bool "Fix tree log recovery"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MULTIPLE_WRITEBACK
	bool "improve grantley random write performance."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_NULL_POINTER_DEREFERENCE_WHEN_MOUNT
	bool "avoid null pointer dereference when mount"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MERGE_HOLES
	bool "file hole can be merged with both previous and next hole items"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RECLAIM_SPACE
	bool "add support for doing defrag on nocow file"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_PUNCH_HOLE_ENOSPC
	bool "fix punch hole no space when split leaf, may lead to BUG_ON"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_FALLOCATE_FAIL
	bool "fix btrfs fallocate fail when throttling at last extent"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_SNAPSHOT_HANG
	bool "fix snapshot hang"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_ASYNC_PAGECACHE_RA
	bool "btrfs send uses async page cache readhead to accelerate"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BACKREF
	bool "provide backref walking mechanism framework"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SNAPSHOT_SIZE_CALCULATION
	bool "add ioctl to calculate actual disk size of snapshots"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_BACKREF

config SYNO_BTRFS_AVOID_CACHE_BLOCK_GROUP_SOFT_LOCKUP
	bool "Avoid soft lockup when cache_block_group with mount option nospace_cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DELAYED_DATA_REF_OOM
	bool "fix oom when huge amount of file extent (running iozone + snapshot)"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DEFAULT_SAPCE_CACHE_V2
	bool "default space cache v2"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REDUCE_LOCK_CONTENTION_IMPROVE_IOPS
	bool "reduce lock contention for fs-tree and csum-tree with random write IOPS"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_MOUNT_OPTION_COMMIT_1S_NO_EFFECT
	bool "fix mount option commit=1 no effect, when race transaction blocked, will sleep 5s"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CHECK_INTEGRITY
	bool "Auto fix error in check_leaf()."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_TRIM_ENOSPC
	bool "Fix trim will lead to ENOSPC and lose data."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_BUG_WEHN_QGROUP_ATOMIC_ALLOC_FAILED
	bool "Fix bug when qgroup ulist_node atomic alloc failed."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ENHANCE_PERFORMANCE_AND_LATENCY
	bool "enhance random write performance and latency"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IOC_SYNC_SYNO
	bool "add a new ioctl BTRFS_IOC_SYNC_SYNO for iscsi"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SKIP_FIND_CLONE
	bool "add a send flag to skip find_extent_clone process"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_ENHANCE_SMALL_FILE
	bool "enhance send/receive for small file"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_FALLOCATE_SUPPORT
	bool "to fallocate to pre-allocate file extents as sending subvols"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_FALLBACK_COMPRESSION
	bool "add a send flag to convert file compression algorithm from zstd to lzo"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_FIX_FIEMAP_RESULT_NOT_CORRECTED
	bool "fix fiemap result not corrected"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SOFT_LOCKUP_AVOIDANCE
	bool "avoid soft lock-up during sending"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SYSFS_BLOCK_GROUP_CNT
	bool "add sysfs interface about block group count information"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SYSFS_FREE_SPACE_TREE
	bool "add sysfs interface about free space tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_ENHANCE_RMDIR
	bool "enhance send/receive for rmdir"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_JOURNAL_INFO_BUG
	bool "fix btrfs journal_info bug"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_SEND_PANIC
	bool "fix btrfs send panic when commit root invalid memory access"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLONE_RANGE_V2
	bool "clone range v2 version"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_FALLOCATE_RUN_BLOCKING_DELAYEDREF
	bool "Not running blocking async delayed ref for fallocate"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_UNUSED_HINT
	bool "FIHINTUNUSED ioctl to send free space information to underly layers"
	default y
	depends on SYNO_MD_UNUSED_HINT
	depends on BTRFS_FS

config SYNO_BTRFS_UNLOCKED_BUFFER_WRITE
	bool "Btrfs: implement unlocked buffered write"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COW_ASYNC_THROTTLE
	bool "enhance latency for cow with async throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ORDERED_EXTENT_THROTTLE
	bool "enhance latency and avoid OOM with ordered extent throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SKIP_BLOCK_GROUP
	bool "add mount option to skip block group"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_PERF_STATS
	bool "Btrfs performance stats"
	default y
	depends on BTRFS_FS && DEBUG_FS

config SYNO_BTRFS_SKIP_QUOTA_TREE
	bool "Skip reading qgroup/usrquota tree during mount"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIMIT_BIO_SIZE_FOR_LATENCY
	bool "Btrfs limit bio size max 64k for latency"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SPLIT_ENDIO_WORKQUEUE_FOR_ORDERED_EXTENT
	bool "Btrfs split endio workqueue for ordered extent to enhance latency"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DATA_LOSE_WHEN_NOSPACE
	bool "fix snapshot data lose with nocow file when nospace"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LOG_TREE_USE_SINGLE_METADATA
	bool "log tree use single metadata instead of dup"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LOG_TREE_RSV_METADATA
	bool "reserve metadata group for log tree"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_METADATA_RESERVE && SYNO_BTRFS_CLUSTER_ALLOCATION

config SYNO_BTRFS_MITIGATE_SNAP_LIVELOCK
	bool "Btrfs mitigate snapshot livelock"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ALLOW_SNAPSHOT_DELETE_STOP
	bool "Support btrfs cleaner stop deleting snapshot"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_RENAME_DIR_ITEM_COLLISION_ERROR
	bool "Fix dir item collision in rename would cause abort transaction"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_ERROR_PAGE_DOUBLE_CLEAR
	bool "Fix page double clear in the error case for nocow"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_CRASH_WITH_ANON_BDEV_WHEN_MEMORY_ALLOCATE_FAILED
	bool "Fix snapshot create failed when memory allocate failed"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DATA_SPACE_INFO_UNDERFLOW_WITH_FALLOCATE_DUE_TO_QUOTA_RESERVE_FAILED
	bool "Fix data space info underflow with fallocate due to quota reserve failed"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_RANGE_CLONE_ENHANCE
	bool "Enhance send range clone reduce space usage"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_DELAYED_REF_THROTTLE
	bool "improve btrfs delayed ref throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_SPACE_CACHE
	bool "improve space cache, not necessary update free space inode"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_NON_BLOCKING_PUNCH_HOLE
	bool "Add non_blocking_punch_hole"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_IMPROVE_DELAYED_REF_THROTTLE

config SYNO_BTRFS_FIX_DATA_CHUNK_ALLOCATE_TOO_MUCH_FOR_PARALLEL_WRITE
	bool "fix data chunk allocate too much for parallel write"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_TUNE_DEFAULT_MAX_INLINE_SIZE
	bool "tune default max inline size"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FEATURE_METADATA_CACHE
	bool "add metadata cache feature with ssd cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DEBUG_UMOUNT_HANG
	bool "Enable btrfs umount debug, it should be removed after bug verified"
	default n
	depends on BTRFS_FS

config SYNO_BTRFS_VFS_INO_TO_PATH
	bool "Query ino to path in vfs"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_NO_LOG_REPLAY
	bool "Allow nologreplay mount option on rw volume mount path"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FEATURE_SPACE_USAGE
	bool "add space usage feature for each type, each subvol has own type"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_FIX_SRCU_IGET_DEADLOCK

config SYNO_BTRFS_COMMIT_STATS
	bool "Btrfs performance stats about commit transaction."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DISABLE_SUBVOL_QUOTA
	bool "Enable/Disable the quota system upon individual subvolumes"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_FAST_QGROUP

config SYNO_BTRFS_COMPRESSION
	bool "Btrfs syno compression"
	default y
	depends on SYNO_FS_COMPRESSION && BTRFS_FS

config SYNO_BTRFS_DELAYED_REF_RESERVED_REWORK
	bool "Rework delayed ref reserved"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT
	bool "Btrfs mount option expand to 64-bit"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPAT_RO_NO_REMOUNT_RW
	bool "No remount as read-write is allowed for compat-ro"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FALLOCATE_MARK_WRITTEN
	bool "Mark fallocated area as written rether than prealloc"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FREE_SPACE_ANALYZE
	bool "A new ioctl to analyze btrfs free space"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RESET_SCRUB_WORKQUEUE_WHEN_SCRUB_FINISHED
	bool "Reset scrub workqueue when scrub finished"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CACHE_PROTECTION
	bool "syno cache protection for btrfs"
	default y
	depends on BTRFS_FS && SYNO_FS_CACHE_PROTECTION

config SYNO_BTRFS_FIX_SRCU_IGET_DEADLOCK
	bool "Fix deadlock caused by grabbing srcu lock and then wait for inode being freed"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IS_INODE_INLINE
	bool "Test function to test if inode inline"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_STAT

config SYNO_BTRFS_DELAYED_ORPHAN_CLEANUP_WHEN_MOUNT
	bool "improve mount time with dealyed orphan cleanup for tree-root"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REDUCE_SNAPSHOT_CRITICAL_SECTION
	bool "Reduce snapshot critical section"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BACKPORT
	bool "backport only small part of a large patch"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REMOVE_FLAG_TREE_CHECK
	bool "remove tree checker machanism for inode item flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_PAGE_REFERENCE_LEAK
	bool "fix page reference leak"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SCRUB_CANCEL
	bool "Let btrfs cancel scrubbing faster"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_FLAGS_SUPPORT
	bool "Support syno btrfs send flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG
	bool "Add syno_flag to file extent"
	default n
	depends on BTRFS_FS && SYNO_BTRFS_REMOVE_FLAG_TREE_CHECK

config SYNO_BTRFS_DEDUPE
	bool "syno btrfs dedupe"
	default n
	depends on BTRFS_FS && SYNO_BTRFS_FIX_SRCU_IGET_DEADLOCK && SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG

config SYNO_BTRFS_DROP_LOG_TREE
	bool "add mount option to drop log tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DROP_PROGRESS_INCONSISTENT
	bool "fix drop progress inconsistent when drop snapshot error"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ASYNC_DATA_FLUSH
	bool "improve latency with async flush"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_MULTIPLE_WRITEBACK

config SYNO_BTRFS_ASYNC_METADATA_FLUSH_AND_THROTTLE
	bool "improve latency with async metadata flush and throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLEANER_THROTTLE
	bool "Throttle cleaner"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SYNO_QUOTA
	bool "Syno quota v2"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_FAST_QGROUP && SYNO_BTRFS_USRQUOTA

config SYNO_BTRFS_FEATURE_TREE
	bool "synology feature tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CAPABILITY_FLAGS
	bool "synology capability flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RBD_META
	bool "Reserve SynoRBD meta in filesystem"
	default y
	depends on BTRFS_FS && SYNO_FS_RBD_META && SYNO_BTRFS_FEATURE_TREE && SYNO_BTRFS_CAPABILITY_FLAGS

config SYNO_BTRFS_BALANCE_DRY_RUN
	bool "add btrfs balance dry run"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_PARTIAL_WRITE_END_DEADLOCK
	bool "fix partial write end deadlock"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIST_HARDLINKS
	bool "list hardlinks with inum"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ALLOCATOR
	bool "synology btrfs allocator"
	default y
	depends on BTRFS_FS
	select SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT

config SYNO_BTRFS_SKIP_RESERVE_WHEN_NO_QUOTA_LIMIT
	bool "synology btrfs skip quota reserve when no quota limit"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_USRQUOTA

config SYNO_BTRFS_MOUNT_STATS
	bool "Btrfs performance stats about mount."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_UUID_CHECKING
	bool "Fix unnecessary uuid rescan"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_UNNECESSARY_FLUSH_WITH_OLD_SIZE_IS_ZERO_WHEN_TRUNCATE
	bool "Fix unnecessary flush with old size is zero when truncate"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIMIT_PRE_RUN_DELAYED_REFS_FOR_COMMIT_TRANSACTION
	bool "Limit pre-run delayed-refs for commit transaction"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_FIEMAP_FOR_LARGE_SPARSE_FILE
	bool "Fiemap improve for large sparse file"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_HIBERNATION_MONITOR
	bool "Monitor modified log for hibernation"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_CHUNK_LOGICAL_OVERFLOW
	bool "fix chunk logical overflow"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_BIO_FLAG_REQ_META_MISSING
	bool "fix bio flag REQ_META missing"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_STATISTICS
	bool "add meta statistics"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QUOTA_SOFT_LIMIT
	bool "add quota soft limit"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_IMPROVE_CHECK_NEW_DIR_CREATED_WITH_NEW_DIR_CACHE
	bool "improve check new dir created with new dir cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AUTO_DISABLE_COMPRESS_WHEN_NOCOW_SET
	bool "auto disable compress when nocow set"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QUICK_BALANCE
	bool "balance only one block group"
	default y
	depends on SYNO_BTRFS_ALLOCATOR

config SYNO_BTRFS_SEARCH_BY_EXTENT_TYPE
	bool "search extent item by extent type"
	default y
	depends on SYNO_BTRFS_BACKREF

config SYNO_BTRFS_FREE_SPACE_CACHE_IMPROVEMENT
	bool "free space cache improvement"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DELETE_SNAPSHOT_CHECK
	bool "delete snapshot should check whether it is snapshoted by someone"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_ORPHANED_RUUID
	bool "do not set ruuid on orphaned subvolume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_NOCOW_WRITE_WHEN_VOLUME_FULL
	bool "improve nocow write when volume full for iSCSI thick lun"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_XATTR_EXTENSION_ENOSPC
	bool "fix xattr extension enospc"
	default y
	depends on BTRFS_FS

endmenu #BTRFS

menu "ECRYPT"

config SYNO_ECRYPTFS_STAT
	bool "Ecryptfs SYNOStat"
	default y
	depends on SYNO_FS_STAT && ECRYPT_FS

config SYNO_ECRYPTFS_ARCHIVE_BIT
	bool "Ecryptfs archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && ECRYPT_FS

config SYNO_ECRYPTFS_ARCHIVE_VERSION
	bool "Ecryptfs archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && ECRYPT_FS

config SYNO_ECRYPTFS_CREATE_TIME
	bool "Ecryptfs syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && ECRYPT_FS

config SYNO_ECRYPTFS_SKIP_EDQUOT_WARNING
	bool "Ecryptfs skip EDQUOT, ENOSPC warning log"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_SKIP_AUTH_WARNING
	bool "Ecryptfs add ratelimit to auth tok not found error message"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_REMOVE_TRUNCATE_WRITE
	bool "Speed up ecryptfs truncate by skipping zeros write"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_CHECK_SYMLINK_LENGTH
	bool "Check ecryptfs symlink target length after encryption"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_FILENAME_SYSCALL
	bool "System calls to get encrypt or decrypt filename"
	default y
	depends on SYNO_SYSTEM_CALL && ECRYPT_FS

config SYNO_ECRYPTFS_WINACL
	bool "Enable syno acl in ecryptfs"
	default y
	depends on ECRYPT_FS && SYNO_FS_WINACL

config SYNO_ECRYPTFS_OCF
	bool "enable ocf framework"
	default n
	depends on ECRYPT_FS && OCF_OCF

config SYNO_ECRYPTFS_LOWER_INIT
	bool "Ecryptfs always initial lower file with rw, ignore security check on initialization"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_REDUCE_MEMCPY
	bool "Reduce one memcpy on ecryptfs for performance."
	default y
	depends on ECRYPT_FS && BTRFS_FS && SYNO_FS_RECVFILE

config SYNO_ECRYPTFS_SKIP_KERNEL_WRITE_CHECK
	bool "Skip security check during kernel_write."
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_FAST_LOOKUP
	bool "Fast lookup, read i_size from xattr"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_PASS_BTRFS_IOCTL
	bool "Pass syno btrfs ioctl to lower btrfs filesystem"
	default y
	depends on ECRYPT_FS && BTRFS_FS

config SYNO_ECRYPTFS_SKIP_EQUAL_ISIZE_UPDATE
	bool "Update ecryptfs i_size only when they are different"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_EXPORT
	bool "Enable ecryptfs nfs export"
	default y
	depends on ECRYPT_FS
	depends on EXPORTFS

config SYNO_ECRYPTFS_FALLOCATE_SUPPORT
	bool "Add fallocate for eCryptfs"
	default y
	depends on ECRYPT_FS && SYNO_ECRYPTFS_REMOVE_TRUNCATE_WRITE

config SYNO_ECRYPTFS_AVOID_MOUNT_REPEATLY
	bool "Avoid ecryptfs mount repeatly at the same mount point"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_RECREATE_CORRUPT_HEADER
	bool "Recreate corrupted header of lower file"
	default y
	depends on ECRYPT_FS

endmenu #ECRYPT
menu "NFS"

config SYNO_NFSD_WRITE_SIZE_MIN
	int "NFSD min packet size"
	default 131072

config SYNO_NFSD_UDP_PACKET
	bool "Provide a interface for user to set the udp packet size they want"
	default y

config SYNO_NFSD_UDP_MAX_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default 32768
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_UDP_MIN_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default	4096
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_UDP_DEF_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default 8192
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_UNIX_PRI
	bool "Provide a interface for user to enable command chmod or not on ACL share"
	default y

config SYNO_NFS4_DISABLE_UDP
	bool "disable NFSv4 over UDP"
	default y

config SYNO_NFS_VAAI_SUPPORT
	bool "NFS VAAI support"
	default y

config SYNO_NFS_VAAI_LAZY_CLONE
	bool "NFS VAAI lazy clone support"
	default y
	depends on BTRFS_FS && SYNO_NFS_VAAI_SUPPORT

config SYNO_NFSD_HIDDEN_FILE
	bool "Hide system directories"
	default y

config SYNO_NFSD_AVOID_HUNG_TASK_WHEN_UNLINK_BIG_FILE
	bool "Avoid parent mutex hung task when unlink big file"
	default y

config SYNO_NFSD_SQUASH_TO_ADMIN
	bool "Grant permission of administrators group to admin user"
	default y

config SYNO_NFSD_NUMA_SVC_POOL_PERNODE
	bool "Enhance NFS performance for numa model"
	default y
	depends on NUMA

config SYNO_NFSD_LATENCY_REPORT
	bool "Add /proc/net/rpc/nfsd_lat to monitor nfsd latency"
	default y

config SYNO_NFSD_SYNO_FILE_STATS
	bool "Add /proc/fs/nfsd/syno_file_stats to spot check file write"
	default y
	depends on NFSD

config SYNO_NFSD_CONNECTION_STAT
	bool "Mmonitor number of nfsd connection stat"
	default y
	depends on NFSD && SYNO_NFSD_LATENCY_REPORT

config SYNO_NFSD_UDC_COLLECTOR
	bool "Collect information for UDC"
	default y
	depends on NFSD && SYNO_NFSD_LATENCY_REPORT

config SYNO_NFSD_SHARE_QUOTA
	bool "Support share quota"
	default y
	depends on NFSD && SYNO_FS_QUOTA_QUERY

config SYNO_NFSD_TRACE
	bool "Tracepoint for debug and collecting data"
	default y
	depends on NFSD

endmenu #NFS

menu "HFSPLUS"

config SYNO_HFSPLUS_MAX_FILENAME_CHECK
	bool "HFS+ return error when filename's length > 255"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_DONT_ZERO_ON_NEW_FILE
	bool "HFS+ don't zero a newly created file"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_ADD_MUTEX_FOR_VFS_OPERATION
	bool "HFS+ add mutex lock on all vsf operation"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_ERROR_HANDLE_ENHANCE
	bool "HFS+ change some WARN_ON to warning message and RO fs"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_SHOW_CASELESS_INFO
	bool "HFS+ show caseless option"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_CASELESS_CREATE_BY_NEW_NAME
	bool "HFS+ create caseless dentry by new name"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_EA
	bool "HFS+ enable EA support"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_BREC_FIND_RET_CHECK
	bool "Check brec_find return value while update parent"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_NODE_DEBUG
	bool "Print info if have invalid offset"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_CREATE_TIME
	bool "assign create time to vfs inode structure"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_NFC_WORKAROUND
	bool "workaround of read on-disk nfc filename"
	default y
	depends on HFSPLUS_FS

endmenu #HFSPLUS

menu "UDF"
config SYNO_UDF_CASELESS
	bool "UDF use caseless lookup"
	default y
	depends on UDF_FS

config SYNO_UDF_UINT_UID_GID
	bool "UDF uses unsigned int UID/GID"
	default y

endmenu #UDF

menu "ISOFS"

config SYNO_ISOFS_UINT_UID_GID
	bool "ISOFS uses unsigned int UID/GID"
	default y

endmenu #ISOFS

menu "FUSE"

config SYNO_FUSE_GLUSTER
	bool "Let fuse handle gluster specifically"
	default y
	depends on FUSE_FS

config SYNO_FUSE_STAT
	bool "Gluster FS support synostat and caseless stat"
	default y
	depends on FUSE_FS && SYNO_FS_STAT && SYNO_FS_CASELESS_STAT

config SYNO_FUSE_ARCHIVE_VERSION
	bool "Gluster FS support archive verion"
	default y
	depends on FUSE_FS && SYNO_FS_ARCHIVE_VERSION

config SYNO_FUSE_CREATE_TIME
	bool "Gluster FS support syno create time"
	default y
	depends on FUSE_FS && SYNO_FS_CREATE_TIME

config SYNO_FUSE_WINACL
	bool "Gluster FS support synoacl"
	default y
	depends on FUSE_FS && SYNO_FS_WINACL && SYNO_FS_ARCHIVE_BIT && SYNO_FUSE_GLUSTER

config SYNO_FUSE_ARCHIVE_BIT
	bool "Gluster FS support syno archive bit"
	default y
	depends on FUSE_FS && SYNO_FS_ARCHIVE_BIT && SYNO_FUSE_GLUSTER

endmenu #FUSE

menu "ConfigFS"

config SYNO_FS_CONFIGFS_BACKPORT
	bool "backport commits for configfs"
	default y

config SYNO_CONFIGFS_SIMPLE_ATTR_SIZE_AS_PAGE_SIZE
    bool "set SIMPLE_ATTR_SIZE as PAGE_SIZE"
    default n
    depends on CONFIGFS_FS

endmenu #ConfigFS

menu "sysfs"

config SYNO_SYSFS_EMIT_OFFSET_WARNON
	bool "skip page offset checking and warnon in sysfs_emit"
	default y
	depends on SYSFS

endmenu #sysfs

endmenu #File Systems
