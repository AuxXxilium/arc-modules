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
./build.sh '*-7.4-*' --tgz          # all DSM 7.4 targets, plus the .tgz packages
./build.sh all                      # every target
```

Options:

| option | meaning |
|---|---|
| `-j N` | make jobs per target (default: the CPUs divided by `-P`) |
| `-P N` | targets built at once (default: 1 for one target, else the CPUs / 4); each has its own work folder and log (`output/logs/<target>.log`), downloads run first and under a lock |
| `-o DIR` | output (default `./output`) |
| `-c DIR` | download cache (default `./cache`) |
| `-w DIR` | work folder for the unpacked trees (default `./work`) |
| `-t DIR` | a syno-compiler folder whose `cache-<dsm>/` already holds the dev toolkits and toolchains (default `/root/syno-compiler`); missing files are downloaded from Synology |
| `-r DIR` | compare with a reference `modules/` folder (e.g. the dev branch's) |
| `--oot a,b` | build only these out-of-tree sources (`none` for none) |
| `--tgz` | also write `output/<target>.tgz` as the release does (flat `.ko` + `VERSION`) |
| `--keep` | keep the unpacked kernel tree of each target in the work folder |
| `--reuse` | reuse the tree a `--keep` run left: only the out-of-tree sources, the checks and the report run again (for working on a recipe) |
| `--strict` | a download without a pinned checksum is an error |
| `--fetch-only` | only download and verify the sources |

Output, per target:

* `output/modules/<target>/*.ko`: the same layout as `modules/<target>/` on
  the release branches (flat, stripped of debug info; geminilakenk also has
  `update/`, as there),
* `output/reports/<target>.txt`: configuration report, out-of-tree results,
  every shipped module with its origin and version, every dropped module with
  the reason, and the comparison with `-r`,
* `output/logs/<target>/`: the build logs.

A target needs about 5 GB in the work folder while it builds; it is removed
afterwards unless `--keep` is given. Inside a target the out-of-tree sources
build four at a time. On the 16-CPU build host, 4 targets at once build
`all` in about 130 minutes (about 3 minutes per target; the Mellanox OFED
build alone takes several minutes on the 4.4 platforms that get it); one
at a time had taken 6.7 minutes per 4.4 target and about 9 per 5.10 target
before OFED. A target built either way is byte-identical. Requirements: bash, curl, tar, xz,
python3, make, gcc (host tools), patch, git, flock.

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
   Before that, every header of the source that differs from the official
   kernel's (the toolkit's `build/include`, `build/arch/x86/include`) is
   replaced by the official one, so modules see the official structs even
   where the source is a newer or another platform's (see platforms.conf),
   and `config/<series>/patches/*.patch` are applied (fixes that let an
   in-tree driver build against Synology's tree, and on 5.10 the device IDs
   the prebuilt e1000e, igc, i2c-i801 and snd-hda-intel knew that 5.10.55's
   lack: patches 0007-0010).
   Bools the official kernel has off that something in the fragment selects
   (IRQ_POLL on the nk kernels, or the HDMI/SYNC_FILE a DRM driver needs on
   4.4) are not an error: what selects them is turned off and listed in the
   report, so a driver the official kernel cannot carry is left out instead
   of changing the kernel's ABI. The ones whose code can live in a module of
   its own (power_supply, devlink, page_pool, dimlib) are allowed, and that
   module is built (see Out-of-tree sources).
3. **Release**: `LOCALVERSION` makes the tree's release the official one
   (`4.4.302+`, `5.10.55+`), so every module gets the official vermagic.
4. **In-tree modules**: `make -k modules` in the configured tree (vmlinux is
   never built; the official one is what loads the modules). One object that
   fails keeps kbuild from linking any module, so `scripts/prune.py` maps
   each failed object to the symbol the fragment added for it, the build
   turns that symbol (and whatever of ours selects it) off and runs again,
   incrementally, until the tree builds. The report lists every symbol
   turned off with the first compiler error. An official module that does
   not compile (only seen where the source is newer than the official
   kernel, against the official headers) cannot be turned off, its symbol is
   Synology's: it is taken out of its Makefile instead and DSM's own copy is
   the one that loads (unless official.list names it, which fails the build).
5. **Out-of-tree modules**: every `src/<name>/` whose `SERIES` matches,
   built against the same tree.
6. **Collect** (`scripts/collect.py`): what ships is every module the
   official kernel does not have, the official modules named in
   `config/<series>/official.list` (arc loads them early), and what those
   depend on. An out-of-tree module replaces the in-tree one of the same
   name, except a helper built only from the target's own kernel source
   (marked `.kernel-source`), which is the same code: where the kernel build
   made that module, the in-tree one stays. An in-tree module that depends
   on a replaced one was compiled against the replaced module's structures
   and exports (these kernels have no symbol versions to catch that) and is
   dropped. Each shipped module must carry the official vermagic and
   resolve every undefined symbol from vmlinux (the toolkit's
   `Module.symvers`) or another shipped module; a module that does not is
   dropped and listed in the report with the symbols it lacks.

## Verified (2026-11-27, build host: `./build.sh --strict --tgz all`)

Every target of platforms.conf except epyc7003ntb (no source, see below)
builds; `-r` compared each with the dev branch's `modules/<target>/`.
"vermagic differs" counts modules both sets have whose vermagic is not
the same (0 everywhere); "dropped" are built modules the symbol or
vermagic check refused (every shipped one resolves all its symbols);
"turned off" are symbols turned off because the driver does not compile
here or needs a bool the official kernel has off whose code cannot live in
a module (DRM on the 4.4 platforms without it, IRQ_POLL on the nk kernels,
...); "headers replaced" are source headers replaced by the official ones.
`scripts/summary.py output/reports` prints this table. Every module the
prebuilt set has is built, except the ones built into the official vmlinux
and a few of DSM's own (see "What does not come from source").

| target | shipped | prebuilt | both | vermagic differs | dropped | turned off | oot failed | headers replaced |
|---|---|---|---|---|---|---|---|---|
| apollolake-7.2-4.4.302 | 483 | 408 | 407 | 0 | 0 | 3 | - | 1 |
| apollolake-7.3-4.4.302 | 483 | 408 | 407 | 0 | 0 | 3 | - | 0 |
| apollolake-7.4-4.4.302 | 483 | 408 | 407 | 0 | 0 | 3 | - | 0 |
| broadwell-7.2-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 1 |
| broadwell-7.3-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwell-7.4-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwellnk-7.2-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 1 |
| broadwellnk-7.3-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwellnk-7.4-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwellnkv2-7.2-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 1 |
| broadwellnkv2-7.3-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwellnkv2-7.4-4.4.302 | 464 | 365 | 361 | 0 | 0 | 27 | - | 0 |
| broadwellntbap-7.2-4.4.302 | 465 | 368 | 364 | 0 | 0 | 27 | - | 1 |
| broadwellntbap-7.3-4.4.302 | 465 | 368 | 364 | 0 | 0 | 27 | - | 0 |
| broadwellntbap-7.4-4.4.302 | 465 | 368 | 364 | 0 | 0 | 27 | - | 0 |
| denverton-7.2-4.4.302 | 477 | 368 | 367 | 0 | 0 | 25 | - | 1 |
| denverton-7.3-4.4.302 | 477 | 368 | 367 | 0 | 0 | 25 | - | 0 |
| denverton-7.4-4.4.302 | 477 | 368 | 367 | 0 | 0 | 25 | - | 0 |
| epyc7002-7.2-5.10.55 | 557 | 449 | 448 | 0 | 0 | 0 | - | 1 |
| epyc7002-7.3-5.10.55 | 557 | 449 | 448 | 0 | 0 | 0 | - | 0 |
| epyc7002-7.4-5.10.55 | 557 | 449 | 448 | 0 | 0 | 0 | - | 0 |
| epyc7003-7.4-5.10.55 | 555 | 447 | 446 | 0 | 0 | 0 | - | 0 |
| geminilake-7.2-4.4.302 | 479 | 406 | 404 | 0 | 0 | 3 | - | 1 |
| geminilake-7.3-4.4.302 | 479 | 406 | 404 | 0 | 0 | 3 | - | 0 |
| geminilake-7.4-4.4.302 | 479 | 406 | 404 | 0 | 0 | 3 | - | 0 |
| geminilakenk-7.2-5.10.55 | 553 | 457 | 451 | 0 | 0 | 7 | - | 20 |
| geminilakenk-7.3-5.10.55 | 553 | 457 | 451 | 0 | 0 | 3 | - | 0 |
| geminilakenk-7.4-5.10.55 | 553 | 457 | 451 | 0 | 0 | 3 | - | 0 |
| icelaked-7.4-5.10.55 | 549 | 439 | 436 | 0 | 0 | 2 | - | 0 |
| purley-7.2-4.4.302 | 470 | 369 | 364 | 0 | 0 | 29 | - | 1 |
| purley-7.3-4.4.302 | 470 | 369 | 364 | 0 | 0 | 29 | - | 0 |
| purley-7.4-4.4.302 | 470 | 369 | 364 | 0 | 0 | 29 | - | 0 |
| r1000-7.2-4.4.302 | 479 | 374 | 371 | 0 | 0 | 33 | - | 1 |
| r1000-7.3-4.4.302 | 479 | 374 | 371 | 0 | 0 | 33 | - | 0 |
| r1000-7.4-4.4.302 | 479 | 374 | 371 | 0 | 0 | 33 | - | 0 |
| r1000nk-7.2-5.10.55 | 557 | 439 | 438 | 0 | 0 | 7 | - | 19 |
| r1000nk-7.3-5.10.55 | 557 | 439 | 438 | 0 | 0 | 3 | - | 0 |
| r1000nk-7.4-5.10.55 | 557 | 439 | 438 | 0 | 0 | 3 | - | 0 |
| v1000-7.2-4.4.302 | 479 | 377 | 374 | 0 | 0 | 33 | - | 1 |
| v1000-7.3-4.4.302 | 479 | 377 | 374 | 0 | 0 | 33 | - | 0 |
| v1000-7.4-4.4.302 | 479 | 377 | 374 | 0 | 0 | 33 | - | 0 |
| v1000nk-7.2-5.10.55 | 557 | 442 | 441 | 0 | 0 | 7 | - | 19 |
| v1000nk-7.3-5.10.55 | 557 | 442 | 441 | 0 | 0 | 3 | - | 0 |
| v1000nk-7.4-5.10.55 | 557 | 443 | 442 | 0 | 0 | 3 | - | 0 |

GPU modules shipped: i915, i915-compat, dmabuf, drm, ttm, amdgpu on every
5.10 target but geminilakenk (amdgpu, ttm and bochs-drm there, on DSM's own
drm and i915); the official i915 and drm on apollolake and geminilake (4.4),
as in the prebuilt set. No other 4.4 kernel has DRM.

Loaded in DSM 7.2.2-72806 VMs (junior mode, QEMU q35) on DS918+
(apollolake), SA6400 (epyc7002), DS225+ (geminilakenk) and DS725+ (r1000nk):
the new core modules (power_supply, devlink, page_pool, dimlib) and their
users, the InfiniBand stack, geminilakenk's drm/i915 (virtio-gpu and
bochs-drm bound QEMU's devices), the stub-built helpers, binder/ashmem,
tn40xx, i2c-i801, i8042/atkbd, the ACPI button, e1000e and igb (link,
DHCP) loaded through depmod/modprobe and unloaded again, without oops or
warning. No real hardware was used.

## GPU (i915, amdgpu)

* **5.10, all but geminilakenk**: the official kernels have no DRM and are
  built without DMA_SHARED_BUFFER, SYNC_FILE and HDMI, so the kernel's own
  drm.ko cannot load there. `src/drm-backport` builds the owner's
  linux-backport tree (Intel's i915 backports MT_65_24.1.19_PSB_240119.1,
  i915 of DII d8b384342b641, plus amdgpu from 6.5), the way the prebuilt
  i915 was made: dmabuf.ko carries dma-buf, dma-fence and sync_file as a
  module, i915-compat.ko the backport's compat layer, then drm,
  drm_kms_helper, ttm, drm_buddy, drm_display_helper, gpu-sched, i915,
  amdgpu (headless: no display core) and mei for i915's HuC/GSC. The
  official vmlinux has none of these types, so the backport's own
  definitions change nothing it shares; the symbol check proves nothing
  of it needs vmlinux code the official kernel lacks. amdgpu asks the
  power-supply class whether the system is on AC (src/power_supply).
* **geminilakenk**: DRM, DMA_SHARED_BUFFER and i915 are official there. At
  the top level drm, drm_kms_helper and i915 are rebuilt from the same
  source and ship (`config/5.10/geminilakenk.list`), drm with the GEM
  shmem/CMA helpers virtio-gpu, udl and arcpgu need. amdgpu is built
  in-tree (`config/5.10/geminilakenk.config`, display core off, with SI and
  CIK), with ttm, gpu-sched, the ttm helper, bochs-drm, virtio-gpu, udl and
  arcpgu. The backport's i915 stack (dmabuf, i915-compat, drm, drm_kms_helper,
  drm_buddy, drm_display_helper, ttm, intel-gtt, i915, mei) ships in
  `update/` as in the prebuilt set (`src/drm-backport-update`): arc's loader
  takes a module from there first, so GPUs the 5.10 i915 does not know
  (Alder Lake-N/-P, Raptor, Meteor/Arrow Lake, DG2) work. Its dma-buf
  exports are prefixed (dmabufbkpt_), and the intel_gmch_probe/remove
  vmlinux has stay vmlinux's.
* **4.4**: only apollolake and geminilake had i915 in the prebuilt set, and
  there DRM and i915 are official; the official i915/drm are rebuilt from
  the same GPL source and official .config and ship through official.list,
  as before. The other 4.4 kernels have no DRM; the backport targets 5.10
  and is not built for 4.4.

The linux-backport repository is private: the build fetches it at the
pinned commit with a GitHub token from `ARC_GITHUB_TOKEN` (or `AUX_TOKEN`),
handed to git through the environment only. Without a token that one
source fails and the rest of the target still builds.

## What does not come from source

**epyc7003ntb-7.4-5.10.55** is skipped: Synology publishes no kernel source
for it, and epyc7003's differs from its official kernel in 131 headers (its
asm-offsets do not compile against them). Its package stays the prebuilt
one.

Every module the prebuilt packages (`modules/<target>/` on the dev branch)
have was checked against each target's official kernel: its `.config` and
`modules.builtin` from the dev toolkit (the GPL `synoconfigs/` match it on
every platform but broadwell, whose synoconfig is another kernel's), its
`modules.order`, and whether the prebuilt file is one of DSM's own
(Synology-signed). Options the official kernel has off are built as modules
with what they need, the way the prebuilt set does it, as long as vmlinux
has no code depending on them and no struct it shares with modules changes
(power_supply, devlink, page_pool, dimlib, the InfiniBand core without
irq_poll, geminilakenk's drm with the GEM helpers, ...); vmlinux itself is
never changed. What is left of the prebuilt set and is not built here:

| module | where | why |
|---|---|---|
| cpufreq_userspace | every target | built into the official vmlinux (CPU_FREQ_GOV_USERSPACE=y) |
| cpufreq_governor | broadwell*, purley, r1000, v1000 | built in (CPU_FREQ_GOV_COMMON=y; ondemand/conservative too) |
| it87 | broadwell*, purley, icelaked | built in (SENSORS_IT87=y) |
| i8042 | geminilake | built in (SERIO_I8042=y) |
| button | purley | built in (ACPI_BUTTON=y) |
| libphy | r1000, v1000 | built in (PHYLIB=y) |
| i2c-i801 | geminilakenk, icelaked | built in (I2C_I801=y) |
| check_signature, dmabuf | geminilakenk | built in (CHECK_SIGNATURE=y, DMA_SHARED_BUFFER=y) |
| drm_buddy, drm_display_helper | geminilakenk (top level) | modules of the 6.x DRM backport only; they ship in `update/` with it, geminilakenk's 5.10 drm.ko/drm_kms_helper.ko at the top level hold that code |
| qedr | broadwell*, purley | DSM's own (Synology-signed 8.33.9.0, built on DSM's Mellanox OFED): qed/qede ship here from the same 8.33.9.0 source and OFED from the same 4.1 commit, so DSM's qedr loads on them; FastLinQ's qedr Makefile cannot build against OFED 4.1 |

A module built into vmlinux cannot also load as a module (its exports or
its device are already taken), so the prebuilt copies of those were
duplicates.

Also not built, although they are no prebuilt module:

* **mpi3mr on 4.4**: no release builds there. The oldest one (8.5.1,
  45Drives' mirror of Broadcom's source) stops at about 40 places on the
  first pass: the PCI IRQ vector API (`pci_alloc_irq_vectors_affinity()`,
  4.10), blk-mq-pci (4.9), the bsg-lib job interface, `SCSI_SCAN_INITIAL`;
  its interrupt setup is built on the IRQ affinity layer 4.4 does not have.
* **ASoC (snd-soc-*) on 4.4**: Synology's 4.4 has a newer ALSA core than its
  ASoC (`snd_power_lock()` is gone, the regmap cache calls are not declared
  where ASoC expects them): 19 errors in the core alone; nothing shipped
  needs it, and the prebuilt set had none.
* **snd-hda-ext-core on 4.4**: Synology's 4.4 HDA core lacks what it uses
  (`_snd_hdac_chip_read()`); the prebuilt set had none on 4.4 either.

Caveats of modules built against an option vmlinux has off: pages an XDP
redirect hands back to a driver's page_pool (mlx5, bnxt on geminilakenk) are
not recycled by vmlinux's XDP code; devlink cannot reload a device into a
network namespace given by its id (by pid or fd it can); InfiniBand
completion queues that ask for softirq polling are polled from a workqueue.
Modules built on their own against the headers' stubs (pwm-fan, gpio-beeper
without GPIOLIB, qcaspi without OF, phy-mt65xx-usb3, mtk-sd, phy-lgm-usb,
the BMC IPMI drivers) load and bind only the device-tree nodes those
kernels do not have.

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
| `FETCH_INNER` | the driver's tarball inside the download (a vendor zip or source RPM, `scripts/inner.py`): a member pattern, `a!b` for a member of a member; the checksum pins the download |
| `FETCH_UA` | user agent for the download (Supermicro's and Marvell's servers refuse curl's) |
| `SUBDIR` | the folder (inside the source) that holds the Kbuild Makefile |
| `MAKE_ARGS` | extra make arguments, usually `CONFIG_<DRIVER>=m` or a driver's own options; `@KSRC@` and `@RELEASE@` become the kernel tree and release |
| `GIT_URL`, `GIT_COMMIT` | a repository fetched at one commit (the pin); a private one needs `ARC_GITHUB_TOKEN` |
| `BUILD` | `kbuild` (default: `make M=`), `intel` (Intel's own `common.mk`, which generates `kcompat_generated_defs.h` from the kernel headers first), `make` (the vendor's own Makefile, given the toolchain and release; FastLinQ, OFED) or `backport` (a Linux backports tree: `make BACKPORT_DEFCONFIG`, then `make modules`) |
| `UNLESS_OFFICIAL` | symbols; the source is skipped where the official kernel sets one of them |
| `IF_OFFICIAL` | symbols; the source is skipped where the official kernel lacks one of them |
| `INSTALL_DIR` | a folder of the package (`update`) the modules ship in instead of the top level, collected and checked as a set of their own |
| `PLATFORMS` | build only for these platforms |
| `FILES_FROM` | use another recipe's vendored folder (`src/<FILES_FROM>`) |
| `KERNEL_FILES` | files (shell patterns) copied from the target's kernel source (a renamed in-tree driver, a helper) |
| `oot_prepare()` | optional shell function run in the copied source before make; `OOT_KSRC` (the kernel tree), `OOT_OFFICIAL_CONFIG` (the official kernel's configuration), `OOT_SERIES`, `OOT_RELEASE` and `s` (the recipe folder) are set |

Kinds of sources here:

* vendor drivers, vendored: Realtek r8169 (from dsm_linux_fix), Broadcom
  mpt3sas 46.00 (4.4) and 55.00 (5.10) and bnxt_en 1.8.54 (4.4), HP hpsa
  (4.4),
* GPU: drm-backport (pinned commit of a private repository, see GPU above),
* vendor drivers, pinned downloads: Intel igb/ixgbe/ixgbevf/i40e/iavf/ice
  (newest release on 5.10, the last one that builds on 4.4; on 4.4 also
  Intel's prefixed intel_auxiliary bus) and e1000e 3.8.7 with PTP and the
  4.16 82574 MSI-X fixes (4.4); Tehuti tn40xx 006 with every PHY; Realtek r8125 9.018.00, r8126 10.018.00, r8127 11.017.02,
  r8152 2.22.1, r8168 8.057.00 (with small 4.4 shims); Marvell atlantic
  2.5.12 and FastLinQ qed/qede/qedr 8.59.6.0 (8.33.9.0 qed/qede where DSM
  ships its own FastLinQ); Broadcom megaraid_sas 07.731.01.00, mpt3sas 53.00
  (5.10 without IRQ_POLL) and mpi3mr 8.19.1 (5.10); Mellanox OFED
  4.1-1.0.2.0 (mlx_compat, the InfiniBand core, mlx4/mlx5) where DSM ships
  it; Microchip smartpqi 2.1.42-011, Motorcomm yt6801, aqc111 (4.4),
  WireGuard and exFAT for 4.4, igc for 4.4, Coral gasket/apex for 4.4,
  frankcrawford's it87 (where the official kernel has no it87),
  ashmem/binder (anbox, 4.4), AIC8800 aic_load_fw, acpi_call, tty0tty, WCH
  ch35x_38x,
* kernel drivers built under another name or outside their Kconfig, from
  the target's own source: r8169_lk (the kernel's r8169 next to
  Realtek's), the r1000/v1000 phylib drivers (tg3, b44, asix, lan78xx and
  the Broadcom/Realtek/Microchip PHYs, src/phylib_mdio), binder and ashmem
  on 5.10 (the unexported functions they use found by name at load time),
* options the official kernel has off, built as modules of their own the
  way the prebuilt set has them, where vmlinux has no code depending on
  them and no struct it shares changes (`config/5.10/abi-allow.list` turns
  them on for the drivers): power_supply, devlink, page_pool, dimlib; the
  InfiniBand core without irq_poll (patches/0006), drivers built against the
  headers' stubs (pwm-fan, gpio-beeper, qcaspi, phy-mt65xx-usb3, mtk-sd,
  phy-lgm-usb, the BMC IPMI drivers),
* kernel helpers the official vmlinux leaves out (their bool is off, or a
  tristate without a prompt nothing selects), built as modules from the
  target's own source: check_signature, textsearch, hvc_console,
  blk-mq-virtio, ipmi_plat_data/ipmi_dmi, pldmfw, sdhci-pci-data,
  intel_idle, i8042, atkbd, cpufreq ondemand/conservative with their common
  code, efifb/vesafb (apollolake, geminilake), the framebuffer drawing
  helpers (cfb*, sys*, fb_sys_fops), snd-pcm-dmaengine, snd-compress,
  snd-hda-ext-core (5.10), pcspkr/pcspeaker, the USB PHY layer (phy),
  i2c-smbus, regmap-mmio (5.10), virtio_dma_buf (geminilakenk).

Vendored files live in `src/<name>/files/` (every series) or
`src/<name>/<series>/` (one series). `src/<name>/module-<series>.conf`,
`src/<name>/Kbuild` or `Kbuild-<series>` and `patches/[<series>/]*.patch`
adjust one series.
