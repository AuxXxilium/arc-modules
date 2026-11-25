# arc-modules: source

This branch holds the source of arc's kernel module packages and the script
that builds them, so the packages no longer depend on prebuilt binaries.

It builds against the **official** Synology kernels, the ones DSM ships:

| series  | kernel    | platforms |
|---------|-----------|-----------|
| 4.4     | 4.4.302+  | apollolake, broadwell, broadwellnk, broadwellnkv2, broadwellntbap, denverton, geminilake, purley, r1000, v1000 (DSM 7.2, 7.3, 7.4) |
| 5.10    | 5.10.55+  | epyc7002, geminilakenk, r1000nk, v1000nk (7.2, 7.3, 7.4), epyc7003, epyc7003ntb, icelaked (7.4) |

`platforms.conf` lists every target, named `<platform>-<dsm>-<kernel>` like
the `modules/<target>/` folders of the release branches and the
`<target>.tgz` packages arc downloads.

The modules for arc's own *custom* kernel (dsm_linux_fix) are not built here;
that kernel builds its modules itself.

## Usage

On a Linux x86_64 host (the build host: `/root/arc-modules-src/repo`):

```sh
./build.sh                          # list the targets
./build.sh apollolake-7.2-4.4.302   # one target
./build.sh 'epyc7002-*'             # a pattern (quote it)
./build.sh '*-7.4-*' -j 8 --tgz     # all DSM 7.4 targets, plus the .tgz packages
./build.sh all                      # every target
```

Options:

| option | meaning |
|---|---|
| `-j N` | make parallelism (default 8) |
| `-o DIR` | output (default `./output`) |
| `-c DIR` | download cache (default `./cache`) |
| `-w DIR` | work folder for the unpacked trees (default `./work`) |
| `-t DIR` | a syno-compiler folder whose `cache-<dsm>/` already holds the dev toolkits and toolchains (default `/root/syno-compiler`); missing files are downloaded from Synology |
| `-r DIR` | compare with a reference `modules/` folder (e.g. the dev branch's) |
| `--oot a,b` | build only these out-of-tree sources (`none` for none) |
| `--tgz` | also write `output/<target>.tgz` as the release does (flat `.ko` + `VERSION`) |
| `--keep` | keep the unpacked kernel tree of each target in the work folder |
| `--strict` | a download without a pinned checksum is an error |
| `--fetch-only` | only download and verify the sources |

Output, per target:

* `output/modules/<target>/*.ko`: the same layout as `modules/<target>/` on
  the release branches (flat, stripped of debug info),
* `output/reports/<target>.txt`: configuration report, out-of-tree results,
  every shipped module with its origin and version, every dropped module with
  the reason, and the comparison with `-r`,
* `output/logs/<target>/`: the build logs.

A target needs about 5 GB in the work folder while it builds; it is removed
afterwards unless `--keep` is given. Requirements: bash, curl, tar, xz,
python3, make, gcc (host tools), patch, kmod.

## How a target is built

1. **Inputs** (cached, sha256 pinned in `checksums.sha256`):
   * the Synology GPL kernel source of the platform and DSM build
     (`Synology NAS GPL Source/<build>/<platform>/linux-<series>.x.txz`),
   * the dev toolkit `ds.<platform>-<dsm>.dev.txz`: the official kernel's
     `.config`, `Module.symvers`, `modules.order` and release string,
   * the toolchain `<platform>-gcc1220_glibc236_x86_64-GPL.txz` (GCC 12.2,
     the compiler the official kernels were built with).
2. **Configuration**: the official `.config` (the toolkit's, which differs
   from the source's `synoconfigs/` in `CONFIG_MODULE_SIG`, part of `struct
   module`), run through the source's Kconfig once (the *baseline*), then the
   fragment `config/<series>/common.config` and
   `config/<series>/<platform>.config` (optional). A symbol the baseline
   already sets keeps Synology's value.
   `scripts/configcheck.py` then compares the result with the baseline:
   n → m is what the fragment is for; n → y is allowed only for symbols in
   `config/<series>/abi-allow.list`; anything else (a builtin turned module,
   a bool that changes a struct the official kernel shares) fails the build.
   Before that, `config/<series>/patches/*.patch` are applied to the source
   (fixes that let an in-tree driver build against Synology's tree).
   Bools the official kernel has off that something in the fragment selects
   (on some platforms NET_DEVLINK, PAGE_POOL, DIMLIB, IRQ_POLL, or the
   HDMI/SYNC_FILE a DRM driver needs) are not an error: what selects them is
   turned off and listed in the report, so a driver the official kernel
   cannot carry is left out instead of changing the kernel's ABI.
3. **Release**: `LOCALVERSION` makes the tree's release the official one
   (`4.4.302+`, `5.10.55+`), so every module gets the official vermagic.
4. **In-tree modules**: `make -k modules` in the configured tree (vmlinux is
   never built; the official one is what loads the modules). One object that
   fails keeps kbuild from linking any module, so `scripts/prune.py` maps
   each failed object to the symbol the fragment added for it, the build
   turns that symbol (and whatever of ours selects it) off and runs again,
   incrementally, until the tree builds. The report lists every symbol
   turned off with the first compiler error.
5. **Out-of-tree modules**: every `src/<name>/` whose `SERIES` matches,
   built against the same tree.
6. **Collect** (`scripts/collect.py`): what ships is every module the
   official kernel does not have, the official modules named in
   `config/<series>/official.list` (arc loads them early), and what those
   depend on. An out-of-tree module replaces the in-tree one of the same
   name. Each shipped module must carry the official vermagic and resolve
   every undefined symbol from vmlinux (the toolkit's `Module.symvers`) or
   another shipped module; a module that does not is dropped and listed in
   the report with the symbols it lacks.

## What does not come from source (yet)

Compared with the prebuilt packages (`modules/<target>/` on the dev
branch), these are not built here, and why:

* **GPU on 5.10** (drm, i915, i915-compat, dmabuf, amdgpu, ttm, ...): the
  official 5.10 kernels other than geminilakenk are built without
  DMA_SHARED_BUFFER, HDMI and SYNC_FILE, bools drm needs in vmlinux. The
  prebuilt set used a backport that carries its own dma-buf; that is not
  ported. geminilakenk's own i915 is official and DSM ships it.
* **Core bools the prebuilt set had turned into modules**: power_supply,
  devlink, page_pool, dimlib, regmap-mmio, intel_idle, cpufreq_governor,
  efifb/vesafb, ... They change structs the official kernel shares; the
  drivers that need them (typec/tcpm, gpio-charger; on some platforms mlx4/5,
  bnxt, ice, qed, megaraid_sas, mpt3sas) are turned off there and listed.
  Small helpers that change no struct are built (check_signature, textsearch,
  hvc_console, blk-mq-virtio, ipmi_plat_data, pldmfw, sdhci-pci-data).
* **Needs vmlinux code the official kernel lacks**: kvm-intel on 5.10
  (l1tf_mitigation, asm_exc_nmi_noist), rtc-cmos (HPET RTC emulation),
  fbcon on 4.4 (VT_HW_CONSOLE_BINDING), sata_sil on 4.4 (Synology's libata
  hooks), scsi_transport_fc/libfc on 4.4 (SCSI_NETLINK), nf_socket/tproxy on
  5.10 (udp4_lib_lookup).
* **Does not compile against Synology's 4.4 tree**: snd-soc-*, the realtek
  HDA codec, pmbus (Synology hooks missing), mpi3mr and smartpqi (need
  blk-mq-pci, 4.9+).
* **No source here**: yt6801 (Motorcomm), aic_load_fw (AIC8800 USB
  firmware loader), aqc111 for 4.4, binder/ashmem (Android), mlx_compat and
  the Mellanox OFED/Marvell FastLinQ vendor stacks (qed/qede 8.59, qedr):
  the in-tree drivers are built instead. atkbd, i8042 and pcspkr are DSM's
  own where the platform has them.
* **Older or other versions than prebuilt**: Realtek r8125 9.016.01 (prebuilt
  9.018.00), r8126 10.017.00 (10.018.00), r8168 8.056.02 (8.057.00), r8152
  2.21.4 (2.22.1) are the newest the dsm_linux_fix tree carries; atlantic is
  GitHub's AQtion 2.5.5 (prebuilt 2.5.12 from Marvell's site); e1000e, bnxt_en,
  megaraid_sas, qed/qede on 4.4 are the kernel's own.

## Layout

```
build.sh                 the build
platforms.conf           targets: platform, DSM, kernel, GPL/toolchain builds
checksums.sha256         sha256 of every download, by cache path
config/<series>/
  common.config          the fragment for every platform of the series
  <platform>.config      optional additions for one platform
  official.list          official modules that ship anyway
  exclude.list           modules never shipped
  <platform>.list        optional: +name / -name for one platform
  abi-allow.list         symbols allowed to go n -> y
  patches/*.patch        fixes applied to the GPL kernel source
src/<name>/              out-of-tree sources, see below
scripts/                 configcheck.py, prune.py, collect.py, compare.py,
                         kmod.py, pin-checksums.sh (pins new downloads)
```

## Out-of-tree sources

Each `src/<name>/module.conf` is a shell fragment:

| field | meaning |
|---|---|
| `ORIGIN` | where the source comes from (repository and commit, or release) |
| `SERIES` | kernel series it builds for (`"4.4 5.10"`) |
| `FETCH_URL`, `FETCH_KEY` | a pinned download instead of vendored files (`FETCH_KEY` names it in the cache, its sha256 is in `checksums.sha256`); `FETCH_STRIP` components are stripped (default 1) |
| `SUBDIR` | the folder (inside the source) that holds the Kbuild Makefile |
| `MAKE_ARGS` | extra make arguments, usually `CONFIG_<DRIVER>=m` |
| `BUILD` | `kbuild` (default: `make M=`) or `intel` (Intel's own `common.mk`, which generates `kcompat_generated_defs.h` from the kernel headers first) |
| `KERNEL_FILES` | files copied from the target's kernel source (a renamed in-tree driver) |
| `oot_prepare()` | optional shell function run in the copied source before make |

Kinds of sources here:

* vendor drivers, vendored: Realtek r8125/r8126/r8127/r8152/r8168/r8169
  (from dsm_linux_fix), Broadcom mpt3sas and mpi3mr, HP hpsa (4.4),
* vendor drivers, pinned downloads: Intel igb/ixgbe/ixgbevf/i40e/iavf/ice
  (newest release on 5.10, the last one that builds on 4.4), Aquantia
  atlantic, WireGuard and exFAT for 4.4, igc for 4.4, Coral gasket/apex for
  4.4, acpi_call, tty0tty, WCH ch35x_38x,
* kernel drivers built under another name: r8169_lk (the kernel's r8169
  next to Realtek's),
* kernel helpers the official vmlinux leaves out (their bool is off), built
  as modules from the target's own source: check_signature, textsearch,
  hvc_console, blk-mq-virtio, ipmi_plat_data/ipmi_dmi, pldmfw.

Vendored files live in `src/<name>/files/` (every series) or
`src/<name>/<series>/` (one series). `src/<name>/module-<series>.conf`,
`src/<name>/Kbuild` or `Kbuild-<series>` and `patches/[<series>/]*.patch`
adjust one series.
