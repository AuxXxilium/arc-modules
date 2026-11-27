# arc-ofed.mk: configure and build MLNX OFED's kernel part the way its spec
# file does (configure, then "make kernel"), with the modules DSM's own build
# has plus the user verbs/MAD interfaces and the RDMA CM. Between the two,
# the fixes Synology's 4.4 needs go onto the backported sources (configure
# applies OFED's backport patches first).
export ARCH CROSS_COMPILE
# configure puts CROSS_COMPILE in front of CC and LD itself; the kernel
# build takes them from CROSS_COMPILE too
MAKEOVERRIDES := $(filter-out CC=% LD=%,$(MAKEOVERRIDES))
unexport CC LD

all:
	env -u CC -u LD ./configure --kernel-version=$(KVER) --kernel-sources=$(KSRC) \
	  --with-core-mod --with-user_mad-mod --with-user_access-mod --with-addr_trans-mod \
	  --with-mlx4-mod --with-mlx4_en-mod --with-mlx5-mod -j4
	# Synology's 4.4 has get_user_pages(tsk, mm, start, nr, gup_flags, pages,
	# vmas), which none of the compat probes matches: the gup_flags form,
	# called with tsk and mm
	grep -q "define HAVE_GET_USER_PAGES_GUP_FLAGS" compat/config.h || \
	  echo "#define HAVE_GET_USER_PAGES_GUP_FLAGS 1" >>compat/config.h
	sed -i 's|^\t\tret = get_user_pages(cur_base,|\t\tret = get_user_pages(current, current->mm, cur_base,|' \
	  drivers/infiniband/core/umem.c
	[ "$$(grep -c 'get_user_pages(current, current->mm, cur_base,' drivers/infiniband/core/umem.c)" = 2 ]
	# without NET_CLS_ACT (the official kernels) there is no struct
	# tc_action: the compat header's no-tc_vlan stubs then
	sed -i 's|^#ifndef HAVE_TC_VLAN_H$$|struct tc_action;\n#if !defined(HAVE_TC_VLAN_H) \|\| !defined(CONFIG_NET_CLS_ACT)|' \
	  include/net/tc_act/tc_vlan.h
	grep -q 'defined(CONFIG_NET_CLS_ACT)' include/net/tc_act/tc_vlan.h
	$(MAKE) kernel
