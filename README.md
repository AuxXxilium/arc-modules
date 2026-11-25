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
