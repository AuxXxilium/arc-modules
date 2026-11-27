#!/usr/bin/env bash
#
# Copyright (C) 2026 AuxXxilium <https://github.com/AuxXxilium>
#
# This is free software, licensed under the MIT License.
# See /LICENSE for more information.
#
# Build arc's kernel module packages from source, against the official
# Synology kernels (4.4.302 and 5.10.55), for every platform/DSM target in
# platforms.conf. See README.md.
#
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Defaults, all overridable by options or the environment.
JOBS="${JOBS:-}"
PARALLEL="${PARALLEL:-}"
NPROC="$(nproc 2>/dev/null || echo 4)"
CACHE_DIR="${CACHE_DIR:-${ROOT}/cache}"
WORK_DIR="${WORK_DIR:-${ROOT}/work}"
OUT_DIR="${OUT_DIR:-${ROOT}/output}"
# A syno-compiler checkout whose cache-<dsm>/ folders already hold the dev
# toolkits and toolchains. Used read-only; anything missing is downloaded.
TOOLKIT_DIR="${TOOLKIT_DIR:-/root/syno-compiler}"
COMPARE_DIR="${COMPARE_DIR:-}"
KEEP=0
REUSE=0
STRICT=0
TGZ=0
FETCH_ONLY=0
ONLY_OOT=""

GPL_URL="https://global.synologydownload.com/download/ToolChain/Synology%20NAS%20GPL%20Source"
TOOLKIT_URL="https://global.synologydownload.com/download/ToolChain/toolkit"
TOOLCHAIN_URL="https://global.synologydownload.com/download/ToolChain/toolchain"
GCCLIB="gcc1220_glibc236"
CROSS="x86_64-pc-linux-gnu-"

###############################################################################
usage() {
  cat <<EOF
Usage: $0 [options] [target ...]

Targets are <platform>-<dsm>-<kernel> names from platforms.conf, or a shell
pattern over them (quote it): 'apollolake-*', '*-7.4-*', '*-5.10.55', all.
Without a target, the list of targets is printed.

Options:
  -j N            make parallelism per target (default: the CPUs divided
                  by the targets built at once)
  -P N            targets built at once (default: one for a single target,
                  else the CPUs / 4, at least 1)
  -o DIR          output folder (default ${OUT_DIR}); modules land in
                  DIR/modules/<target>/, reports in DIR/reports/
  -c DIR          download cache (default ${CACHE_DIR})
  -w DIR          work folder for the unpacked trees (default ${WORK_DIR})
  -t DIR          syno-compiler folder with cache-<dsm>/ toolkits
                  (default ${TOOLKIT_DIR}; missing files are downloaded)
  -r DIR          compare against a reference modules/ folder (for example
                  the modules/ folder of the dev branch) and report the
                  difference per target
  --oot NAMES     build only these out-of-tree sources (comma separated) on
                  top of the in-tree modules; "none" skips them all
  --tgz           also pack DIR/<target>.tgz the way the release does
  --keep          keep the unpacked kernel tree of each target
  --reuse         reuse a tree --keep left (only out-of-tree, checks, report)
  --strict        a source without a pinned checksum is an error
  --fetch-only    only download and verify the sources of the targets
  -h, --help      this text
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
warn() { printf '[%s] WARN: %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
die()  { printf '[%s] ERROR: %s\n' "$(date +%H:%M:%S)" "$*" >&2; exit 1; }

###############################################################################
# platforms.conf

declare -A T_PLATFORM T_DSM T_KVER T_GPL T_GPLPLAT T_TCBUILD T_TCDIR
TARGETS=()
load_platforms() {
  local p d k g b n
  while IFS=';' read -r p d k g b n; do
    [[ -z "${p}" || "${p}" == \#* ]] && continue
    local t="${p}-${d}-${k}"
    TARGETS+=("${t}")
    T_PLATFORM[${t}]="${p}"; T_DSM[${t}]="${d}"; T_KVER[${t}]="${k}"
    # "<build>" or "<build>/<platform>": a target without a GPL source of
    # its own (Synology publishes none) borrows another one's
    T_GPL[${t}]="${g%%/*}"; T_GPLPLAT[${t}]="${p}"
    [[ "${g}" == */* ]] && T_GPLPLAT[${t}]="${g#*/}"
    T_TCBUILD[${t}]="${b}"; T_TCDIR[${t}]="${n}"
  done <"${ROOT}/platforms.conf"
}

series_of() { # 4.4.302 -> 4.4, 5.10.55 -> 5.10
  echo "${1%.*}"
}

###############################################################################
# Downloads and checksums. checksums.sha256 pins every file by its path
# relative to the cache (gpl/..., toolkit/..., oot/...).

sha_pinned() {
  awk -v f="$1" '$2 == f || $2 == "*"f { print $1; exit }' "${ROOT}/checksums.sha256" 2>/dev/null || true
}

verify() { # verify <file> <key>
  local file="$1" key="$2" want have
  want="$(sha_pinned "${key}")"
  if [ -z "${want}" ]; then
    have="$(sha256sum "${file}" | awk '{print $1}')"
    [ "${STRICT}" -eq 1 ] && die "no pinned checksum for ${key} (sha256 ${have})"
    warn "no pinned checksum for ${key}, add: ${have}  ${key}"
    return 0
  fi
  [ -n "${VERIFIED[${file}]:-}" ] && return 0
  have="$(sha256sum "${file}" | awk '{print $1}')"
  [ "${have}" = "${want}" ] || die "checksum mismatch for ${key}: ${have} != ${want}"
  VERIFIED[${file}]=1
}
declare -A VERIFIED

# Targets built at once share the cache: a download (or a checkout) runs
# under one lock, so two targets never write the same file.
cache_lock() {
  mkdir -p "${CACHE_DIR}"
  exec {CACHE_LOCK_FD}>"${CACHE_DIR}/.lock"
  flock "${CACHE_LOCK_FD}"
}
cache_unlock() {
  flock -u "${CACHE_LOCK_FD}"
  exec {CACHE_LOCK_FD}>&-
}

fetch() { # fetch <url> <dest> <key> [user agent]
  local url="$1" dest="$2" key="$3" ua=()
  [ -n "${4:-}" ] && ua=(-A "$4")
  cache_lock
  if [ ! -s "${dest}" ]; then
    mkdir -p "$(dirname "${dest}")"
    # A server can answer 200 with an empty or a wrong body (git.zx2c4.com
    # did): only a file with the pinned checksum goes into the cache, so a
    # bad answer never stays there for the next target.
    local try want have
    want="$(sha_pinned "${key}")"
    for try in 1 2 3; do
      log "download ${url}$([ "${try}" -gt 1 ] && echo " (try ${try})")"
      rm -f "${dest}.part"
      if curl -fL --retry 3 --connect-timeout 30 "${ua[@]}" -o "${dest}.part" "${url}" && [ -s "${dest}.part" ]; then
        have="$(sha256sum "${dest}.part" | awk '{print $1}')"
        [ -z "${want}" ] || [ "${have}" = "${want}" ] && break
        warn "${key}: got sha256 ${have}, pinned ${want}"
      fi
      rm -f "${dest}.part"
      sleep $((try * 10))
    done
    [ -s "${dest}.part" ] || die "download failed: ${url}"
    mv -f "${dest}.part" "${dest}"
  fi
  cache_unlock
  verify "${dest}" "${key}"
}

# fetch_git <url> <commit> <dir>: a repository at one commit (the pin). A
# private one needs a token in ARC_GITHUB_TOKEN or AUX_TOKEN; it reaches git
# through the environment, never a command line or a log.
fetch_git() {
  local url="$1" commit="$2" dir="$3"
  cache_lock
  fetch_git_locked "${url}" "${commit}" "${dir}"
  local rc=$?
  cache_unlock
  return "${rc}"
}
fetch_git_locked() {
  local url="$1" commit="$2" dir="$3" token="${ARC_GITHUB_TOKEN:-${AUX_TOKEN:-}}"
  if [ -d "${dir}/.git" ] && [ "$(git -C "${dir}" rev-parse HEAD 2>/dev/null)" = "${commit}" ]; then
    return 0
  fi
  log "fetch ${url} at ${commit}"
  rm -rf "${dir}.part"; mkdir -p "${dir}.part"
  git -C "${dir}.part" init -q
  if [ -n "${token}" ]; then
    GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0="http.https://github.com/.extraheader" \
      GIT_CONFIG_VALUE_0="AUTHORIZATION: basic $(printf 'x-access-token:%s' "${token}" | base64 -w0)" \
      GIT_TERMINAL_PROMPT=0 git -C "${dir}.part" fetch -q --depth 1 "${url}" "${commit}"
  else
    GIT_TERMINAL_PROMPT=0 git -C "${dir}.part" fetch -q --depth 1 "${url}" "${commit}"
  fi || { rm -rf "${dir}.part"; warn "cannot fetch ${url} at ${commit} (a private repository needs ARC_GITHUB_TOKEN)"; return 1; }
  git -C "${dir}.part" -c advice.detachedHead=false checkout -q FETCH_HEAD
  [ "$(git -C "${dir}.part" rev-parse HEAD)" = "${commit}" ] || { warn "${url}: got $(git -C "${dir}.part" rev-parse HEAD), not ${commit}"; rm -rf "${dir}.part"; return 1; }
  rm -rf "${dir}"; mv "${dir}.part" "${dir}"
}

urlenc() { # spaces and parentheses, as Synology's folders need
  printf '%s' "$1" | sed 's/ /%20/g; s/(/%28/g; s/)/%29/g'
}

# Inputs of a target: the GPL kernel source, the dev toolkit (official
# kernel's .config/Module.symvers/modules.order) and the toolchain.
gpl_file() {
  local t="$1"
  echo "gpl/${T_GPL[${t}]}/${T_GPLPLAT[${t}]}/linux-$(series_of "${T_KVER[${t}]}").x.txz"
}
toolkit_file() {
  local t="$1"
  echo "toolkit/${T_DSM[${t}]}/ds.${T_PLATFORM[${t}]}-${T_DSM[${t}]}.dev.txz"
}
toolchain_file() {
  local t="$1"
  echo "toolchain/${T_TCBUILD[${t}]}/${T_PLATFORM[${t}]}-${GCCLIB}_x86_64-GPL.txz"
}

# Prefer the syno-compiler cache for toolkit files when it has them.
local_toolkit() { # local_toolkit <dsm> <basename>
  local f="${TOOLKIT_DIR}/cache-$1/$2"
  [ -s "${f}" ] && echo "${f}" || true
}

fetch_inputs() { # sets IN_GPL IN_TOOLKIT IN_TOOLCHAIN
  local t="$1" key f
  key="$(gpl_file "${t}")"
  IN_GPL="${CACHE_DIR}/${key}"
  fetch "${GPL_URL}/${T_GPL[${t}]}/${T_GPLPLAT[${t}]}/$(basename "${key}")" "${IN_GPL}" "${key}"

  key="$(toolkit_file "${t}")"
  f="$(local_toolkit "${T_DSM[${t}]}" "$(basename "${key}")")"
  if [ -n "${f}" ]; then
    IN_TOOLKIT="${f}"; verify "${f}" "${key}"
  else
    IN_TOOLKIT="${CACHE_DIR}/${key}"
    fetch "${TOOLKIT_URL}/${T_DSM[${t}]}/${T_PLATFORM[${t}]}/$(basename "${key}")" "${IN_TOOLKIT}" "${key}"
  fi

  key="$(toolchain_file "${t}")"
  f="$(local_toolkit "${T_DSM[${t}]}" "$(basename "${key}")")"
  if [ -n "${f}" ]; then
    IN_TOOLCHAIN="${f}"; verify "${f}" "${key}"
  else
    IN_TOOLCHAIN="${CACHE_DIR}/${key}"
    fetch "${TOOLCHAIN_URL}/${T_TCBUILD[${t}]}/$(urlenc "${T_TCDIR[${t}]}")/$(basename "${key}")" "${IN_TOOLCHAIN}" "${key}"
  fi
}

###############################################################################
# Kernel tree: the GPL source configured with the official .config plus our
# fragment. Modules built in it carry the official vermagic and struct
# layout; the check further down proves every one of them resolves.

KMAKE=()
prepare_tree() {
  local t="$1" w="$2" series rel kv lv
  series="$(series_of "${T_KVER[${t}]}")"
  rm -rf "${w}"
  mkdir -p "${w}/linux" "${w}/toolkit" "${w}/toolchain" "${w}/oot" "${w}/logs"

  log "${t}: unpack toolchain"
  tar -xaf "${IN_TOOLCHAIN}" -C "${w}/toolchain" --strip-components=1
  log "${t}: unpack dev toolkit"
  tar -xaf "${IN_TOOLKIT}" -C "${w}/toolkit" --strip-components=9 --wildcards \
    "usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-${T_DSM[${t}]}/build/.config" \
    "usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-${T_DSM[${t}]}/build/Module.symvers" \
    "usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-${T_DSM[${t}]}/build/include/*"     "usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-${T_DSM[${t}]}/build/arch/x86/include/*" \
    "usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-${T_DSM[${t}]}/modules.*"
  [ -s "${w}/toolkit/build/.config" ] || die "${t}: the dev toolkit has no build/.config"

  log "${t}: unpack GPL kernel source"
  tar -xaf "${IN_GPL}" -C "${w}/linux" --strip-components=1
  # The headers modules see must be the official kernel's. The toolkit has
  # them (build/include, build/arch/x86/include); where the source's differ
  # (a 7.3 source for a 7.2 kernel, a borrowed platform, or Synology's own
  # drift) the toolkit's version replaces the source's, and the report lists
  # every header replaced.
  (cd "${w}/toolkit/build" && find include arch/x86/include -name '*.h' -type f \
      -not -path '*/generated/*' -not -path 'include/config/*' | sort | while read -r h; do
        if [ ! -f "${w}/linux/${h}" ]; then
          mkdir -p "$(dirname "${w}/linux/${h}")"
        elif cmp -s "${h}" "${w}/linux/${h}"; then
          continue
        fi
        cp -f "${h}" "${w}/linux/${h}"
        echo "${h}"
      done) >"${w}/headers.txt"
  # config/<series>/patches/*.patch: fixes to in-tree drivers so they build
  # against Synology's tree. One that does not apply (a platform whose
  # source differs) is reported, not fatal: its driver then fails to
  # compile and is turned off like any other.
  : >"${w}/patches.txt"
  local p
  for p in "${ROOT}/config/${series}/patches"/*.patch; do
    [ -f "${p}" ] || continue
    if patch -d "${w}/linux" -p1 -s -N <"${p}" >>"${w}/logs/patches.log" 2>&1; then
      echo "applied $(basename "${p}")" >>"${w}/patches.txt"
    else
      echo "NOT applied $(basename "${p}")" >>"${w}/patches.txt"
      warn "${t}: $(basename "${p}") does not apply"
    fi
  done

  export PATH="${w}/toolchain/bin:${PATH}"
  KMAKE=(make -C "${w}/linux" ARCH=x86_64 CROSS_COMPILE="${CROSS}")

  # vermagic: the official release (4.4.302+, 5.10.55+) is the Makefile's
  # version plus a "+" Synology's git tree added; give it back through
  # LOCALVERSION.
  rel="$(cat "${w}/toolkit/build/include/config/kernel.release")"
  kv="$(make -s -C "${w}/linux" kernelversion 2>/dev/null)"
  lv="${rel#"${kv}"}"
  [ "${kv}${lv}" = "${rel}" ] || die "${t}: kernel ${kv} cannot give release ${rel}"
  KMAKE+=(LOCALVERSION="${lv}")
  echo "${rel}" >"${w}/release"

  configure_tree "${t}" "${w}" "${series}"

  log "${t}: modules_prepare"
  "${KMAKE[@]}" modules_prepare >"${w}/logs/prepare.log" 2>&1 \
    || { tail -30 "${w}/logs/prepare.log" >&2; die "${t}: modules_prepare failed"; }
  [ "$(cat "${w}/linux/include/config/kernel.release")" = "${rel}" ] \
    || die "${t}: release $(cat "${w}/linux/include/config/kernel.release") != official ${rel}"
}

# The fragment: config/<series>/common.config then config/<series>/<platform>.config.
# A symbol the official .config already sets (=y or =m) keeps Synology's
# value: the fragment only adds what the official kernel leaves out.
configure_tree() {
  local t="$1" w="$2" series="$3" frag line sym val off
  local cfg="${w}/linux/.config"
  cp "${w}/toolkit/build/.config" "${w}/official.config"
  # The baseline: the official .config run through this source's Kconfig
  # once. Symbols the toolkit predates show up here, not as our change.
  cp "${w}/official.config" "${cfg}"
  "${KMAKE[@]}" olddefconfig >"${w}/logs/config-baseline.log" 2>&1 \
    || { tail -20 "${w}/logs/config-baseline.log" >&2; die "${t}: olddefconfig of the official .config failed"; }
  cp "${cfg}" "${w}/baseline.config"
  : >"${w}/requested.config"
  for frag in "${ROOT}/config/${series}/common.config" "${ROOT}/config/${series}/${T_PLATFORM[${t}]}.config"; do
    [ -f "${frag}" ] || continue
    while IFS= read -r line; do
      line="${line%%#*}"; line="${line//[[:space:]]/}"
      [ -z "${line}" ] && continue
      sym="${line%%=*}"; val="${line#*=}"
      off="$(sed -n "s/^${sym}=//p" "${w}/baseline.config")"
      if [ -n "${off}" ] && [ "${val}" != "n" ]; then
        continue # official value wins
      fi
      sed -i "/^${sym}=/d; /^# ${sym} is not set/d" "${cfg}"
      sed -i "/^${sym}=/d" "${w}/requested.config"
      if [ "${val}" = "n" ]; then
        echo "# ${sym} is not set" >>"${cfg}"
      else
        echo "${sym}=${val}" >>"${cfg}"
      fi
      echo "${sym}=${val}" >>"${w}/requested.config"
    done <"${frag}"
  done
  : >"${w}/pruned.txt"
  finish_config "${t}" "${w}" "${series}"
}

finish_config() {
  local t="$1" w="$2" series="$3" cfg="${w}/linux/.config" round=0
  while :; do
    olddefconfig "${t}" "${w}"
    if python3 -I "${ROOT}/scripts/configcheck.py" "${w}/official.config" "${w}/baseline.config" "${cfg}" \
        "${w}/requested.config" "${ROOT}/config/${series}/abi-allow.list" "${w}/offenders.txt" \
        >"${w}/config-report.txt"; then
      return 0
    fi
    # Bools the official kernel has off that something we added selects
    # (NET_DEVLINK, PAGE_POOL, ... on some platforms): turn off what selects
    # them. Any other ABI change is an error in the fragment.
    round=$((round + 1))
    if [ ! -s "${w}/offenders.txt" ] || [ "${round}" -gt 5 ]; then
      cat "${w}/config-report.txt" >&2
      die "${t}: the configuration changes the official kernel's ABI (see above)"
    fi
    # shellcheck disable=SC2046
    python3 -I "${ROOT}/scripts/prune.py" --selectors "${w}/linux" "${w}/baseline.config" "${cfg}" \
      $(cat "${w}/offenders.txt") | sort -u -t$'\t' -k1,1 \
      | sed 's/$/, which the official kernel has off/' >"${w}/offenders.sel"
    # the bools themselves too: Synology gives some of them a prompt
    # (NET_DEVLINK, PAGE_POOL), and a prompted bool keeps the y it was
    # selected to after its selectors are gone
    sed 's/$/\t-\tthe official kernel has this bool off/' "${w}/offenders.txt" >>"${w}/offenders.sel"
    log "${t}: turning off $(cut -f1 "${w}/offenders.sel" | tr '\n' ' ')(the official kernel has these bools, or what they select, off)"
    turn_off "${t}" "${w}" "${w}/offenders.sel"
  done
}

olddefconfig() { # olddefconfig <target> <work>
  "${KMAKE[@]}" olddefconfig >"${2}/logs/config.log" 2>&1 \
    || { tail -20 "${2}/logs/config.log" >&2; die "${1}: olddefconfig failed"; }
}

# off_lines <work> <file of "SYM<tab>object<tab>reason"> [reason suffix]: set them n
off_lines() {
  local w="$1" list="$2" suffix="${3:-}" sym obj err cfg="${w}/linux/.config"
  while IFS=$'\t' read -r sym obj err; do
    sed -i "/^${sym}=/d; /^# ${sym} is not set/d" "${cfg}" "${w}/requested.config"
    echo "# ${sym} is not set" >>"${cfg}"
    printf '%s\t%s\t%s\n' "${sym}" "${obj}" "${err}${suffix}" >>"${w}/pruned.txt"
  done <"${list}"
}

# turn_off <target> <work> <list> [reason suffix]: set the symbols of the
# list n. One that another enabled symbol selects comes straight back on;
# turn its selectors off too, as far down as it goes.
turn_off() {
  local t="$1" w="$2" list="$3" suffix="${4:-}" cfg="${w}/linux/.config" still round=0 sym
  off_lines "${w}" "${list}" "${suffix}"
  olddefconfig "${t}" "${w}"
  cut -f1 "${list}" >"${list}.want"
  while :; do
    still="$(while read -r sym; do grep -q "^${sym}=" "${cfg}" && echo "${sym}"; done <"${list}.want" || true)"
    [ -z "${still}" ] && break
    round=$((round + 1))
    [ "${round}" -gt 8 ] && die "${t}: cannot turn off ${still}"
    # shellcheck disable=SC2086
    python3 -I "${ROOT}/scripts/prune.py" --selectors "${w}/linux" "${w}/baseline.config" "${cfg}" ${still} \
      | sort -u -t$'\t' -k1,1 >"${list}.sel"
    [ -s "${list}.sel" ] || die "${t}: ${still} stays on and nothing we added selects it"
    off_lines "${w}" "${list}.sel"
    # and the symbols themselves once more: a prompted bool (Synology's
    # NET_DEVLINK) keeps the y a selector gave it after the selector is off
    for sym in ${still}; do
      sed -i "/^${sym}=/d; /^# ${sym} is not set/d" "${cfg}"
      echo "# ${sym} is not set" >>"${cfg}"
    done
    olddefconfig "${t}" "${w}"
  done
}

# Turn symbols off again (a driver that does not compile here).
config_off() { # config_off <target> <work> <series> <file of "SYM<tab>object<tab>error">
  local t="$1" w="$2" series="$3" list="$4"
  turn_off "${t}" "${w}" "${list}"
  finish_config "${t}" "${w}" "${series}"
}

###############################################################################
# Builds

build_intree() {
  local t="$1" w="$2"
  local series pass=1 log
  series="$(series_of "${T_KVER[${t}]}")"
  # One object that does not compile keeps kbuild from ever reaching
  # modpost, and then no module of the tree is linked. So: build with -k,
  # map every failed object to the symbol we added for it (scripts/prune.py),
  # turn those off and build again (incrementally) until the tree is clean.
  while :; do
    log="${w}/logs/intree-${pass}.log"
    log "${t}: in-tree modules, pass ${pass} (-j${JOBS})"
    if "${KMAKE[@]}" -j"${JOBS}" -k modules KBUILD_MODPOST_WARN=1 >"${log}" 2>&1; then
      break
    fi
    [ "${pass}" -ge 8 ] && die "${t}: in-tree build still fails after ${pass} passes, see ${log}"
    python3 -I "${ROOT}/scripts/prune.py" "${w}/linux" "${log}" "${w}/baseline.config" "${w}/linux/.config" \
      >"${w}/prune-${pass}.txt"
    [ -s "${w}/prune-${pass}.txt" ] || { grep -m5 -E 'error|Error' "${log}" >&2; die "${t}: build failed and nothing to prune, see ${log}"; }
    # An official module that does not compile here (a source newer than
    # the official kernel, against its headers): its symbol is Synology's,
    # so it is taken out of its Makefile instead, unless it is one we ship.
    local obj err mod
    while IFS=$'\t' read -r _ obj err; do
      mod="$(basename "${obj}" .o)"
      grep -qx "${mod}" "${ROOT}/config/${series}/official.list" \
        && die "${t}: ${mod} (official, in official.list) does not compile: ${err}"
      sed -i -E "/^\s*obj-/ s#([[:space:]=])$(basename "${obj}")([[:space:]]|\$)#\1\2#" "${w}/linux/$(dirname "${obj}")/Makefile"
      printf '%s\t%s\t%s\n' "(official ${mod})" "${obj}" "${err}; not rebuilt, DSM's own is used" >>"${w}/pruned.txt"
    done < <(grep '^OFFICIAL' "${w}/prune-${pass}.txt" || true)
    grep -v '^OFFICIAL' "${w}/prune-${pass}.txt" >"${w}/prune-${pass}.ours" || true
    log "${t}: turning off $(cut -f1 "${w}/prune-${pass}.ours" | tr '\n' ' ')$(grep -c '^OFFICIAL' "${w}/prune-${pass}.txt" || true) official module(s) skipped"
    if [ -s "${w}/prune-${pass}.ours" ]; then
      config_off "${t}" "${w}" "${series}" "${w}/prune-${pass}.ours"
    fi
    pass=$((pass + 1))
  done
  touch "${w}/intree.done"
}

# Out-of-tree sources: src/<name>/module.conf (see README). The sources are
# either vendored (src/<name>/<series>/ or src/<name>/files/) or a pinned
# download (FETCH_URL + checksums.sha256).
oot_names() {
  local d
  for d in "${ROOT}"/src/*/; do
    [ -f "${d}module.conf" ] && basename "${d}"
  done
}

fetch_oot_sources() { # fetch_oot_sources "<series> ..."
  local name series rc=0
  for name in $(oot_names); do
    if [ -n "${ONLY_OOT}" ]; then
      [ "${ONLY_OOT}" = "none" ] && return 0
      [[ ",${ONLY_OOT}," == *",${name},"* ]] || continue
    fi
    for series in $1; do
      (
        SERIES="" FETCH_URL="" FETCH_KEY="" FETCH_UA="" GIT_URL="" GIT_COMMIT=""
        # shellcheck source=/dev/null
        . "${ROOT}/src/${name}/module.conf"
        [ -f "${ROOT}/src/${name}/module-${series}.conf" ] && . "${ROOT}/src/${name}/module-${series}.conf"
        [[ " ${SERIES} " == *" ${series} "* ]] || exit 0
        if [ -n "${GIT_URL}" ]; then
          fetch_git "${GIT_URL}" "${GIT_COMMIT}" "${CACHE_DIR}/git/${name}-${GIT_COMMIT}"             || { [ "${STRICT}" -eq 1 ] && die "${name}: cannot fetch ${GIT_URL} (private: needs ARC_GITHUB_TOKEN)"; exit 0; }
        elif [ -n "${FETCH_URL}" ]; then
          local key="oot/${FETCH_KEY:-$(basename "${FETCH_URL}")}"
          fetch "${FETCH_URL}" "${CACHE_DIR}/${key}" "${key}" "${FETCH_UA}"
        fi
      ) || rc=1
    done
  done
  return "${rc}"
}

# The sources are independent of each other: OOT_PARALLEL of them build at
# once, each into its own folder, log and report line.
build_oot() {
  local t="$1" w="$2" series name
  series="$(series_of "${T_KVER[${t}]}")"
  : >"${w}/oot-report.txt"
  rm -rf "${w}/oot-report.d"; mkdir -p "${w}/oot-report.d"
  local names=()
  for name in $(oot_names); do
    if [ -n "${ONLY_OOT}" ]; then
      [ "${ONLY_OOT}" = "none" ] && return 0
      [[ ",${ONLY_OOT}," == *",${name},"* ]] || continue
    fi
    names+=("${name}")
  done
  local n=0
  for name in "${names[@]}"; do
    while [ "$(jobs -rp | wc -l)" -ge "${OOT_PARALLEL}" ]; do wait -n || true; done
    # its status is kept; a die() inside (a download) leaves no .rc file
    ( rc=0; build_one_oot "${t}" "${w}" "${series}" "${name}" "${w}/oot-report.d/${name}" || rc=$?
      echo "${rc}" >"${w}/oot-report.d/${name}.rc" ) &
    n=$((n + 1))
  done
  wait || true
  local failed=() rc
  for name in "${names[@]}"; do
    rc="$(cat "${w}/oot-report.d/${name}.rc" 2>/dev/null || echo 1)"
    if [ "${rc}" != 0 ]; then
      failed+=("${name}")
      if ! grep -q "FAILED" "${w}/oot-report.d/${name}" 2>/dev/null; then
        echo "${name}: FAILED (rc ${rc}), see the target's log" >>"${w}/oot-report.d/${name}"
      fi
    fi
    [ -f "${w}/oot-report.d/${name}" ] && cat "${w}/oot-report.d/${name}" >>"${w}/oot-report.txt"
  done
  # --strict: a source that applies to this target and fails (a download, a
  # private repository without a token, a build error) fails the target, so
  # a package never goes out silently without its modules
  if [ ${#failed[@]} -gt 0 ]; then
    [ "${STRICT}" -eq 1 ] && die "${t}: out-of-tree sources failed: ${failed[*]}"
    warn "${t}: out-of-tree sources failed: ${failed[*]}"
  fi
  return 0
}

build_one_oot() {
  local t="$1" w="$2" series="$3" name="$4" rep="$5"
  local s="${ROOT}/src/${name}" d="${w}/oot/${name}" rc=0
  # module.conf variables
  local SERIES="" FETCH_URL="" FETCH_KEY="" FETCH_STRIP=1 FETCH_INNER="" FETCH_UA="" SUBDIR="." MAKE_ARGS="" KERNEL_FILES=""
  local GIT_URL="" GIT_COMMIT="" BACKPORT_DEFCONFIG="" UNLESS_OFFICIAL="" IF_OFFICIAL="" PLATFORMS="" FILES_FROM="" BUILD="kbuild"
  local INSTALL_DIR=""
  unset -f oot_prepare 2>/dev/null || true
  # shellcheck source=/dev/null
  . "${s}/module.conf"
  [ -f "${s}/module-${series}.conf" ] && . "${s}/module-${series}.conf"
  # INSTALL_DIR (e.g. update): the modules ship in that folder of the
  # package, a second set next to the top level one (see collect_subdirs)
  [ -n "${INSTALL_DIR}" ] && d="${w}/oot-${INSTALL_DIR}/${name}"
  [[ " ${SERIES} " == *" ${series} "* ]] || return 0
  if [ -n "${PLATFORMS}" ] && [[ " ${PLATFORMS} " != *" ${T_PLATFORM[${t}]} "* ]]; then
    return 0
  fi
  local sym
  for sym in ${IF_OFFICIAL}; do
    if ! grep -qE "^${sym}=[ym]" "${w}/baseline.config"; then
      echo "${name}: skipped, the official kernel has no ${sym}" >>"${rep}"
      return 0
    fi
  done
  for sym in ${UNLESS_OFFICIAL}; do
    if grep -qE "^${sym}=[ym]" "${w}/baseline.config"; then
      echo "${name}: skipped, the official kernel has ${sym} (its own drivers stay)" >>"${rep}"
      return 0
    fi
  done

  rm -rf "${d}"; mkdir -p "${d}"
  if [ -n "${GIT_URL}" ]; then
    if ! fetch_git "${GIT_URL}" "${GIT_COMMIT}" "${CACHE_DIR}/git/${name}-${GIT_COMMIT}"; then
      echo "${name}: FAILED, cannot fetch ${GIT_URL} at ${GIT_COMMIT} (private: needs ARC_GITHUB_TOKEN)" >>"${rep}"
      return 1
    fi
    # git archive only reads the checkout: no lock needed
    git -C "${CACHE_DIR}/git/${name}-${GIT_COMMIT}" archive HEAD | tar -x -C "${d}"
  elif [ -n "${FETCH_URL}" ]; then
    local key="oot/${FETCH_KEY:-$(basename "${FETCH_URL}")}"
    fetch "${FETCH_URL}" "${CACHE_DIR}/${key}" "${key}" "${FETCH_UA}"
    if [ -n "${FETCH_INNER}" ]; then
      # the driver's tarball inside the vendor's zip or source RPM (the
      # pinned checksum covers the download)
      python3 -I "${ROOT}/scripts/inner.py" "${CACHE_DIR}/${key}" "${FETCH_INNER}" "${d}.inner" >/dev/null \
        && tar -xaf "${d}.inner" -C "${d}" --strip-components="${FETCH_STRIP}" \
        || { rm -f "${d}.inner"; echo "${name}: FAILED, cannot unpack ${FETCH_INNER} from ${key}" >>"${rep}"; return 1; }
      rm -f "${d}.inner"
    else
      tar -xaf "${CACHE_DIR}/${key}" -C "${d}" --strip-components="${FETCH_STRIP}"
    fi
  elif [ -n "${FILES_FROM}" ]; then
    # another recipe's vendored files (src/<FILES_FROM>)
    cp -a "${ROOT}/src/${FILES_FROM}/." "${d}/"
  elif [ -d "${s}/${series}" ]; then
    cp -a "${s}/${series}/." "${d}/"
  elif [ -d "${s}/files" ]; then
    cp -a "${s}/files/." "${d}/"
  fi
  # A source may ship binaries (aic8800's carries ARM modules): only what
  # is built here ships.
  find "${d}" -name '*.ko' -delete
  # Files taken from the kernel source itself (a renamed in-tree driver).
  local f
  # (shell patterns, relative to the kernel source)
  for f in ${KERNEL_FILES}; do
    (cd "${w}/linux" && cp -a ${f} "${d}/") || { echo "${name}: FAILED, ${f} is not in this kernel source" >>"${rep}"; return 1; }
  done
  # Built only from the target's own source: where the kernel build made
  # the same module, collect.py keeps that one.
  if [ -n "${KERNEL_FILES}" ] && [ -z "${FETCH_URL}${GIT_URL}${FILES_FROM}" ] && [ ! -d "${s}/${series}" ] && [ ! -d "${s}/files" ]; then
    touch "${d}/.kernel-source"
  fi
  # patches/*.patch apply to every series, patches/<series>/*.patch to one.
  for f in "${s}"/patches/*.patch "${s}/patches/${series}"/*.patch; do
    [ -f "${f}" ] || continue
    patch -d "${d}" -p1 -s <"${f}" || { echo "${name}: patch $(basename "${f}") failed" >>"${rep}"; return 1; }
  done
  [ -f "${s}/Kbuild-${series}" ] && cp "${s}/Kbuild-${series}" "${d}/${SUBDIR}/Kbuild"
  [ -f "${s}/Kbuild" ] && [ ! -f "${s}/Kbuild-${series}" ] && cp "${s}/Kbuild" "${d}/${SUBDIR}/Kbuild"

  if declare -F oot_prepare >/dev/null; then
    (cd "${d}" && OOT_KSRC="${w}/linux" OOT_SERIES="${series}" OOT_RELEASE="$(cat "${w}/release")" \
      OOT_OFFICIAL_CONFIG="${w}/baseline.config" oot_prepare) \
      >"${w}/logs/oot-${name}.log" 2>&1 || rc=$?
  fi
  # @KSRC@ in MAKE_ARGS: the kernel tree (for Makefiles that probe its headers)
  MAKE_ARGS="${MAKE_ARGS//@KSRC@/${w}/linux}"
  MAKE_ARGS="${MAKE_ARGS//@RELEASE@/$(cat "${w}/release")}"
  if [ "${rc}" -eq 0 ]; then
    log "${t}: out-of-tree ${name}"
    case "${BUILD}" in
      intel)
        # Intel's own build (common.mk): it generates kcompat_generated_defs.h
        # from the kernel headers and then runs kbuild itself.
        make -C "${d}/${SUBDIR}" -j"${OOT_JOBS}" KSRC="${w}/linux" KOBJ="${w}/linux" \
          BUILD_KERNEL="$(cat "${w}/release")" ARCH=x86_64 CROSS_COMPILE="${CROSS}" \
          CC="${CROSS}gcc" LD="${CROSS}ld" KBUILD_MODPOST_WARN=1 ${MAKE_ARGS} \
          >>"${w}/logs/oot-${name}.log" 2>&1 || rc=$?
        ;;
      make)
        # The vendor's own Makefile (it probes the kernel headers and runs
        # kbuild itself): the toolchain and the release go in from here.
        local a lv=""
        for a in "${KMAKE[@]}"; do case "${a}" in LOCALVERSION=*) lv="${a}" ;; esac; done
        make -C "${d}/${SUBDIR}" -j"${OOT_JOBS}" ARCH=x86_64 CROSS_COMPILE="${CROSS}" \
          CC="${CROSS}gcc" LD="${CROSS}ld" ${lv:+"${lv}"} KBUILD_MODPOST_WARN=1 ${MAKE_ARGS} \
          >>"${w}/logs/oot-${name}.log" 2>&1 || rc=$?
        ;;
      backport)
        # A Linux backports tree (Kconfig of its own, CPTCFG_ symbols): its
        # defconfig against this tree's .config, then its own make.
        (cd "${d}/${SUBDIR}" &&
          make KLIB_BUILD="${w}/linux" KLIB="${w}/linux" ARCH=x86_64 CROSS_COMPILE="${CROSS}" \
            "${BACKPORT_DEFCONFIG}" &&
          grep -E '^CPTCFG_[A-Z0-9_]+=m' .config &&
          make -j"${OOT_JOBS}" KLIB_BUILD="${w}/linux" KLIB="${w}/linux" ARCH=x86_64 CROSS_COMPILE="${CROSS}" \
            KBUILD_MODPOST_WARN=1 ${MAKE_ARGS} modules) \
          >>"${w}/logs/oot-${name}.log" 2>&1 || rc=$?
        ;;
      *)
        "${KMAKE[@]}" -j"${OOT_JOBS}" M="${d}/${SUBDIR}" KBUILD_MODPOST_WARN=1 ${MAKE_ARGS} modules \
          >>"${w}/logs/oot-${name}.log" 2>&1 || rc=$?
        ;;
    esac
  fi
  local kos
  kos="$(find "${d}" -name '*.ko' -printf '%f ' | sed 's/\.ko / /g')"
  if [ "${rc}" -ne 0 ] || [ -z "${kos}" ]; then
    echo "${name}: FAILED (rc ${rc}), see logs/oot-${name}.log: $(grep -m3 -E 'error:|Error [0-9]' "${w}/logs/oot-${name}.log" | tr '\n' ' ')" >>"${rep}"
    # nothing of a half-built source ships (ice's auxiliary.ko without ice)
    find "${d}" -name '*.ko' -delete
    return 1
  fi
  echo "${name}: ${kos}" >>"${rep}"
}

###############################################################################
# Collect, check and copy what ships.

collect() {
  local t="$1" w="$2" dst="$3" series
  series="$(series_of "${T_KVER[${t}]}")"
  rm -rf "${dst}"; mkdir -p "${dst}"
  python3 -I "${ROOT}/scripts/collect.py" \
    --linux "${w}/linux" --oot "${w}/oot" --toolkit "${w}/toolkit" \
    --official-list "${ROOT}/config/${series}/official.list" \
    --exclude-list "${ROOT}/config/${series}/exclude.list" \
    --platform-list "${ROOT}/config/${series}/${T_PLATFORM[${t}]}.list" \
    --nm "${CROSS}nm" --strip "${CROSS}strip" \
    --release "$(cat "${w}/release")" --out "${dst}" >"${w}/collect-report.txt" \
    || { tail -20 "${w}/collect-report.txt" >&2; die "${t}: collect failed"; }
  collect_subdirs "${t}" "${w}" "${dst}"
}

# Out-of-tree sources with an INSTALL_DIR ship in that folder of the package
# (update/: arc's loader takes a module from there before the top level one
# of the same name): every module they build, checked like the rest
# (vermagic, no export vmlinux has, every symbol resolved by vmlinux, DSM's
# own modules, the folder itself or a top level module it does not shadow).
collect_subdirs() {
  local t="$1" w="$2" dst="$3" sd sub
  for sd in "${w}"/oot-*/; do
    [ -d "${sd}" ] || continue
    sub="$(basename "${sd}")"; sub="${sub#oot-}"
    [ "${sub}" = "report.d" ] && continue
    {
      echo
      echo "## ${sub}/"
      python3 -I "${ROOT}/scripts/collect.py" \
        --linux "${w}/linux" --oot "${sd%/}" --toolkit "${w}/toolkit" \
        --official-list /dev/null \
        --exclude-list "${ROOT}/config/$(series_of "${T_KVER[${t}]}")/exclude.list" \
        --platform-list /dev/null --nm "${CROSS}nm" --strip "${CROSS}strip" \
        --release "$(cat "${w}/release")" --out "${dst}/${sub}" --subdir-of "${dst}"
    } >>"${w}/collect-report.txt" || { tail -20 "${w}/collect-report.txt" >&2; die "${t}: collect of ${sub}/ failed"; }
  done
}

report() {
  local t="$1" w="$2" dst="$3" r="${OUT_DIR}/reports/${t}.txt"
  mkdir -p "${OUT_DIR}/reports"
  {
    echo "# ${t}"
    echo "release: $(cat "${w}/release")  gpl: ${T_GPL[${t}]}/${T_GPLPLAT[${t}]}  toolchain: ${T_TCBUILD[${t}]}"
    echo "modules: $(find "${dst}" -name '*.ko' | wc -l)"
    echo
    echo "## configuration"
    cat "${w}/config-report.txt"
    echo
    echo "## headers replaced by the official kernel's (toolkit) version: $(wc -l <"${w}/headers.txt")"
    head -40 "${w}/headers.txt"
    echo
    echo "## kernel source patches"
    cat "${w}/patches.txt"
    echo
    echo "## turned off: does not compile against this source (symbol, object, first error)"
    cat "${w}/pruned.txt"
    echo
    echo "## out-of-tree"
    cat "${w}/oot-report.txt"
    echo
    cat "${w}/collect-report.txt"
    if [ -n "${COMPARE_DIR}" ] && [ -d "${COMPARE_DIR}/${t}" ]; then
      echo
      echo "## compared with ${COMPARE_DIR}/${t}"
      python3 -I "${ROOT}/scripts/compare.py" "${COMPARE_DIR}/${t}" "${dst}"
    fi
  } >"${r}"
  mkdir -p "${OUT_DIR}/logs/${t}"
  cp -f "${w}"/logs/*.log "${OUT_DIR}/logs/${t}/" 2>/dev/null || true
  log "${t}: $(sed -n 3p "${r}"), report ${r}"
}

pack() {
  local t="$1" dst="$2"
  date +'%y.%m.%d' >"${dst}/VERSION"
  tar -zcf "${OUT_DIR}/${t}.tgz" -C "${dst}" .
  rm -f "${dst}/VERSION"
}

build_target() {
  local t="$1" w="${WORK_DIR}/${1}" dst="${OUT_DIR}/modules/${1}"
  log "=== ${t}"
  if [ "${T_GPL[${t}]}" = "none" ]; then
    warn "${t}: skipped, Synology publishes no kernel source that matches it (see platforms.conf)"
    return 0
  fi
  fetch_inputs "${t}"
  [ "${FETCH_ONLY}" -eq 1 ] && return 0
  local oldpath="${PATH}"
  if [ "${REUSE}" -eq 1 ] && [ -f "${w}/intree.done" ]; then
    # --reuse: the tree a --keep run left, configured and built; only the
    # out-of-tree sources, the checks and the report run again.
    log "${t}: reusing ${w}"
    export PATH="${w}/toolchain/bin:${PATH}"
    local rel kv
    rel="$(cat "${w}/release")"
    kv="$(make -s -C "${w}/linux" kernelversion 2>/dev/null)"
    KMAKE=(make -C "${w}/linux" ARCH=x86_64 CROSS_COMPILE="${CROSS}" LOCALVERSION="${rel#"${kv}"}")
  else
    prepare_tree "${t}" "${w}"
    build_intree "${t}" "${w}"
  fi
  build_oot "${t}" "${w}"
  collect "${t}" "${w}" "${dst}"
  report "${t}" "${w}" "${dst}"
  [ "${TGZ}" -eq 1 ] && pack "${t}" "${dst}"
  PATH="${oldpath}"
  [ "${KEEP}" -eq 1 ] || rm -rf "${w}"
}

###############################################################################
main() {
  local args=() a
  while [ $# -gt 0 ]; do
    case "$1" in
      -j) JOBS="$2"; shift 2 ;;
      -P) PARALLEL="$2"; shift 2 ;;
      -o) OUT_DIR="$2"; shift 2 ;;
      -c) CACHE_DIR="$2"; shift 2 ;;
      -w) WORK_DIR="$2"; shift 2 ;;
      -t) TOOLKIT_DIR="$2"; shift 2 ;;
      -r) COMPARE_DIR="$2"; shift 2 ;;
      --oot) ONLY_OOT="$2"; shift 2 ;;
      --tgz) TGZ=1; shift ;;
      --keep) KEEP=1; shift ;;
      --reuse) REUSE=1; KEEP=1; shift ;;
      --strict) STRICT=1; shift ;;
      --fetch-only) FETCH_ONLY=1; shift ;;
      -h|--help) usage; exit 0 ;;
      -*) usage; die "unknown option $1" ;;
      *) args+=("$1"); shift ;;
    esac
  done
  load_platforms
  if [ ${#args[@]} -eq 0 ]; then
    printf '%s\n' "${TARGETS[@]}"
    exit 0
  fi
  local sel=() t
  for a in "${args[@]}"; do
    [ "${a}" = "all" ] && a="*"
    local hit=0
    for t in "${TARGETS[@]}"; do
      # shellcheck disable=SC2053
      if [[ "${t}" == ${a} ]]; then sel+=("${t}"); hit=1; fi
    done
    [ "${hit}" -eq 1 ] || die "no target matches ${a} (run $0 without arguments for the list)"
  done
  for cmd in curl tar xz python3 make gcc patch git; do
    command -v "${cmd}" >/dev/null || die "${cmd} is required"
  done
  mkdir -p "${CACHE_DIR}" "${WORK_DIR}" "${OUT_DIR}"
  command -v flock >/dev/null || die "flock is required"
  mapfile -t sel < <(printf '%s\n' "${sel[@]}" | awk '!seen[$0]++')

  # How the CPUs are shared: PARALLEL targets at once, JOBS make jobs each,
  # and inside a target OOT_PARALLEL out-of-tree sources at once.
  if [ -z "${PARALLEL}" ]; then
    if [ ${#sel[@]} -gt 1 ]; then PARALLEL=$((NPROC / 4)); else PARALLEL=1; fi
  fi
  [ "${PARALLEL}" -ge 1 ] || PARALLEL=1
  [ "${PARALLEL}" -le ${#sel[@]} ] || PARALLEL=${#sel[@]}
  [ -n "${JOBS}" ] || JOBS=$((NPROC / PARALLEL))
  [ "${JOBS}" -ge 1 ] || JOBS=1
  OOT_PARALLEL=$(( JOBS >= 4 ? 4 : JOBS ))
  # most sources are a few files; a big one (drm-backport) still gets a
  # good share while the small ones finish next to it
  OOT_JOBS=$(( JOBS / 2 ))
  [ "${OOT_JOBS}" -ge 2 ] || OOT_JOBS=2
  export PARALLEL JOBS OOT_PARALLEL OOT_JOBS
  log "${#sel[@]} target(s): ${PARALLEL} at once, make -j${JOBS} each (${NPROC} CPUs)"

  # The downloads first, one after another, so the targets then only read
  # the cache (the out-of-tree ones are fetched under the cache lock).
  local t
  for t in "${sel[@]}"; do
    [ "${T_GPL[${t}]}" = "none" ] && continue
    ( fetch_inputs "${t}" ) || die "${t}: cannot get its inputs"
  done
  # and the out-of-tree downloads and checkouts of their series, so a server
  # that fails shows up here and not as a package without its modules
  local series_sel
  series_sel="$(for t in "${sel[@]}"; do series_of "${T_KVER[${t}]}"; done | sort -u | xargs)"
  fetch_oot_sources "${series_sel}" || die "cannot get the out-of-tree sources"
  [ "${FETCH_ONLY}" -eq 1 ] && { log "done: inputs of ${#sel[@]} target(s) in ${CACHE_DIR}"; return 0; }

  local start failed=() pid
  declare -A PIDS
  start=$(date +%s)
  mkdir -p "${OUT_DIR}/logs"
  for t in "${sel[@]}"; do
    if [ "${PARALLEL}" -eq 1 ]; then
      ( build_target "${t}" ) || failed+=("${t}")
      continue
    fi
    while [ "$(jobs -rp | wc -l)" -ge "${PARALLEL}" ]; do wait -n || true; done
    log "start ${t} (log ${OUT_DIR}/logs/${t}.log)"
    ( build_target "${t}" ) >"${OUT_DIR}/logs/${t}.log" 2>&1 &
    PIDS[${t}]=$!
  done
  for t in "${!PIDS[@]}"; do
    pid=${PIDS[${t}]}
    if wait "${pid}"; then
      log "${t}: $(grep -m1 -o 'modules: [0-9]*' "${OUT_DIR}/reports/${t}.txt" 2>/dev/null || echo skipped)"
    else
      failed+=("${t}")
      log "${t}: FAILED, $(grep -m1 'ERROR' "${OUT_DIR}/logs/${t}.log" | cut -c1-200)"
    fi
  done
  log "${#sel[@]} target(s) in $(( ($(date +%s) - start) / 60 )) min"
  if [ ${#failed[@]} -gt 0 ]; then
    die "failed targets: ${failed[*]}"
  fi
  log "done: ${#sel[@]} target(s), output in ${OUT_DIR}"
}

main "$@"
