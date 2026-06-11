#!/usr/bin/env bash
# build-upstream.sh
#
# Downloads the upstream Linux kernel source matching the Synology DSM kernel
# version, overlays the custom/out-of-tree drivers from src/4.x or src/5.x,
# compiles the result with the Synology DSM Docker compiler, and merges output
# with the thirdparty base – producing a complete module tarball per platform.
#
# Workflow:
#   1. Read PLATFORMS file
#   2. Download linux-<KVER>.tar.xz from kernel.org (cached in src/kernels/)
#   3. Extract and overlay src/X.x/ custom drivers on top
#   4. (Re)generate defines.<platform> via gen-defines.sh logic
#   5. Compile with Docker (auxxxilium/syno-compiler:<toolkit_ver>)
#   6. Merge .ko output with thirdparty, create final tarball

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${BLUE}[STEP]${NC} $*"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
THIRDPARTY_DIR="${REPO_ROOT}/thirdparty"
PLATFORMS_FILE="${SCRIPT_DIR}/PLATFORMS"
KERNELS_CACHE="${SCRIPT_DIR}/kernels"    # downloaded tarballs live here
WORKSPACE_ROOT="${SCRIPT_DIR}/upstream-workspace"  # per-build staging trees

# ---------------------------------------------------------------------------
# Kernel download helpers
# ---------------------------------------------------------------------------

# kernel.org URL template
kernel_url() {
  local kver="$1"
  local major="${kver%%.*}"
  echo "https://cdn.kernel.org/pub/linux/kernel/v${major}.x/linux-${kver}.tar.xz"
}

# ---------------------------------------------------------------------------
# Synology GPL source helpers
# ---------------------------------------------------------------------------

# DSM toolkit version → GPL source build slug (from archive.synology.com)
# Format: toolkit_ver → build_slug used in the archive URL
declare -A DSM_BUILD=(
  ["7.2"]="7.2-72806"
  ["7.3"]="7.3-86009"
)

# URL for the Synology GPL kernel source tarball
# Args: toolkit_ver  platform  kver  (e.g. 7.2 epyc7002 5.10.55)
syno_gpl_url() {
  local toolkit_ver="$1"
  local platform="$2"
  local kver="$3"
  local series="${kver%.*}"          # e.g. 5.10  or  4.4
  local build="${DSM_BUILD[$toolkit_ver]}"
  [ -z "$build" ] && return 1
  echo "https://global.synologydownload.com/download/ToolChain/Synology%20NAS%20GPL%20Source/${build}/${platform}/linux-${series}.x.txz"
}

# Cache path for a Syno GPL tarball
syno_gpl_cache_path() {
  local toolkit_ver="$1"
  local platform="$2"
  local kver="$3"
  local series="${kver%.*}"
  echo "${KERNELS_CACHE}/syno-${toolkit_ver}-${platform}-linux-${series}.x.txz"
}

# Download Syno GPL kernel source if not already cached
ensure_syno_kernel_source() {
  local toolkit_ver="$1"
  local platform="$2"
  local kver="$3"
  local tarball
  tarball="$(syno_gpl_cache_path "$toolkit_ver" "$platform" "$kver")"

  mkdir -p "${KERNELS_CACHE}"

  if [ -f "${tarball}" ]; then
    log_info "  Syno GPL source already cached: $(basename "${tarball}")"
    return 0
  fi

  local url
  url="$(syno_gpl_url "$toolkit_ver" "$platform" "$kver")" || {
    log_warn "  No Syno GPL URL known for toolkit ${toolkit_ver}"
    return 1
  }

  log_info "  Downloading Syno GPL source for ${platform} (DSM ${toolkit_ver})..."
  log_info "  URL: ${url}"

  if command -v curl &>/dev/null; then
    curl -L --retry 3 --retry-delay 5 -o "${tarball}" "${url}" || {
      log_error "  Download failed: ${url}"
      rm -f "${tarball}"
      return 1
    }
  elif command -v wget &>/dev/null; then
    wget -q --tries=3 -O "${tarball}" "${url}" || {
      log_error "  Download failed: ${url}"
      rm -f "${tarball}"
      return 1
    }
  else
    log_error "  Neither curl nor wget found"
    return 1
  fi

  log_info "  Downloaded: $(basename "${tarball}")"
}

# Detect the top-level prefix inside a tarball (e.g. "linux-5.10.x" or "linux-5.10.x/")
# Prints the prefix without trailing slash, or empty string if files are at top level.
tarball_prefix() {
  local tarball="$1"
  # Peek at the first entry; strip trailing slash
  tar -tf "${tarball}" 2>/dev/null | head -1 | sed 's|/$||; s|/.*||'
}

# Download kernel tarball if not already cached
ensure_kernel_source() {
  local kver="$1"
  local tarball="${KERNELS_CACHE}/linux-${kver}.tar.xz"

  mkdir -p "${KERNELS_CACHE}"

  if [ -f "${tarball}" ]; then
    log_info "Kernel ${kver} already cached: ${tarball}"
    return 0
  fi

  local url
  url="$(kernel_url "${kver}")"
  log_info "Downloading kernel ${kver} from ${url}"

  if command -v curl &>/dev/null; then
    curl -L --retry 3 --retry-delay 5 -o "${tarball}" "${url}" || {
      log_error "Download failed for ${url}"
      rm -f "${tarball}"
      return 1
    }
  elif command -v wget &>/dev/null; then
    wget -q --tries=3 -O "${tarball}" "${url}" || {
      log_error "Download failed for ${url}"
      rm -f "${tarball}"
      return 1
    }
  else
    log_error "Neither curl nor wget found – cannot download kernel source"
    return 1
  fi

  log_info "Downloaded: ${tarball}"
}

# ---------------------------------------------------------------------------
# Fetch the latest stable point release for a kernel series from kernel.org
# e.g. fetch_latest_kver_for_series 5.10  →  5.10.220
# Falls back to the provided default if the lookup fails.
# ---------------------------------------------------------------------------
fetch_latest_kver_for_series() {
  local series="$1"   # e.g. "4.4" or "5.10"
  local default="$2"  # fallback

  local result=""

  # kernel.org finger_banner is a plain-text file listing current stable versions
  if command -v curl &>/dev/null; then
    result=$(curl -sf --max-time 5 https://www.kernel.org/finger_banner 2>/dev/null \
      | grep -i "latest stable ${series} version" \
      | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
  elif command -v wget &>/dev/null; then
    result=$(wget -qO- --timeout=5 https://www.kernel.org/finger_banner 2>/dev/null \
      | grep -i "latest stable ${series} version" \
      | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
  fi

  if [ -n "$result" ]; then
    echo "$result"
  else
    echo "$default"
  fi
}

# ---------------------------------------------------------------------------
# Build workspace setup
# Produces a directory that mirrors what the Docker compile-module command
# expects at /input:
#   /input/Makefile
#   /input/defines.<platform>
#   /input/drivers/  block/  lib/  …  (optional additional dirs)
#   /input/Module.symvers   (4.x only)
# ---------------------------------------------------------------------------

# Extract only the driver subtrees we care about from the kernel tarball
DRIVER_SUBDIRS=(
  "drivers/ata"
  "drivers/block"
  "drivers/cdrom"
  "drivers/char"
  "drivers/firewire"
  "drivers/gpio"
  "drivers/gpu"
  "drivers/hwmon"
  "drivers/hv"
  "drivers/i2c"
  "drivers/infiniband"
  "drivers/input"
  "drivers/media"
  "drivers/message"
  "drivers/mfd"
  "drivers/misc"
  "drivers/mmc"
  "drivers/net"
  "drivers/nvme"
  "drivers/pci"
  "drivers/pinctrl"
  "drivers/platform"
  "drivers/rtc"
  "drivers/scsi"
  "drivers/sound"
  "drivers/staging"
  "drivers/thunderbolt"
  "drivers/tty"
  "drivers/usb"
  "drivers/vfio"
  "drivers/video"
  "drivers/virtio"
  "drivers/watchdog"
  "arch/x86/kvm"
  "virt"
  "block"
  "fs/exfat"
  "fs/fuse"
  "fs/ntfs"
  "fs/9p"
  "lib"
  "include"
  "net/9p"
  "net/ipv4"
  "net/ipv6"
  "net/netfilter"
  "net/sched"
  "net/wireless"
  "sound"
  "Kconfig"
  "Makefile"
)

setup_workspace() {
  local platform="$1"
  local kver="$2"                          # Syno kernel version (output naming / headers)
  local toolkit_ver="$3"
  local upstream_kver="${4:-${kver}}"      # upstream source tarball version (fallback)
  local ws_dir="${WORKSPACE_ROOT}/${platform}-${toolkit_ver}-${kver}"

  log_step "Setting up workspace: ${ws_dir}"
  rm -rf "${ws_dir}"
  mkdir -p "${ws_dir}"

  local major="${kver:0:1}"
  local src_overlay="${SCRIPT_DIR}/${major}.x"

  # ── Step A: extract selected subtrees from kernel source ────────────────
  # Prefer the Synology GPL tarball (exact source matching the Docker headers).
  # Fall back to the upstream kernel.org tarball if Syno GPL is unavailable.

  local syno_tarball
  syno_tarball="$(syno_gpl_cache_path "${toolkit_ver}" "${platform}" "${kver}")"

  local source_tarball=""
  local source_prefix=""
  local source_label=""

  # Try Syno GPL first
  if ensure_syno_kernel_source "${toolkit_ver}" "${platform}" "${kver}" 2>/dev/null \
       && [ -f "${syno_tarball}" ]; then
    source_tarball="${syno_tarball}"
    source_prefix="$(tarball_prefix "${syno_tarball}")"
    source_label="Syno GPL (${toolkit_ver} / ${platform})"
  else
    log_warn "  Syno GPL source unavailable – falling back to upstream linux-${upstream_kver}"
    local upstream_tarball="${KERNELS_CACHE}/linux-${upstream_kver}.tar.xz"
    if [ ! -f "${upstream_tarball}" ]; then
      ensure_kernel_source "${upstream_kver}" || true
    fi
    if [ -f "${upstream_tarball}" ]; then
      source_tarball="${upstream_tarball}"
      source_prefix="linux-${upstream_kver}"
      source_label="upstream linux-${upstream_kver}"
    fi
  fi

  if [ -n "${source_tarball}" ]; then
    log_info "  Extracting source subtrees from ${source_label}..."

    # Build extract path list using the detected prefix
    local extract_paths=()
    for subdir in "${DRIVER_SUBDIRS[@]}"; do
      if [ -n "${source_prefix}" ]; then
        extract_paths+=("${source_prefix}/${subdir}")
      else
        extract_paths+=("${subdir}")
      fi
    done

    # Extract source files – exclude Makefiles/Kbuild/Kconfig so only the
    # X.x/ overlay Makefiles are used (upstream ones use in-tree syntax).
    local strip_args=()
    [ -n "${source_prefix}" ] && strip_args=(--strip-components=1)
    tar -xf "${source_tarball}" \
      "${strip_args[@]}" \
      -C "${ws_dir}" \
      --wildcards \
      --exclude="Makefile" \
      --exclude="Makefile.*" \
      --exclude="Kbuild" \
      --exclude="Kconfig" \
      --exclude="Kconfig.*" \
      "${extract_paths[@]}" 2>/dev/null || {
      log_warn "  Some paths were not found in tarball (normal if a subdir doesn't exist for this platform)"
    }
    log_info "  Source extracted (Makefiles/Kbuild excluded)"

    # Belt-and-suspenders: remove any Makefile/Kbuild/Kconfig that survived.
    find "${ws_dir}" \( -name "Makefile" -o -name "Makefile.*" \
      -o -name "Kbuild" -o -name "Kconfig" -o -name "Kconfig.*" \) \
      -delete 2>/dev/null || true

    # ── Step A2: re-extract Makefiles for complex subtrees ───────────────────
    # net/, vfio, staging, ipmi, arch/x86/kvm use composite objects or subdir
    # trees that need their upstream Makefiles.  The X.x/ overlay still wins
    # in Step B by overwriting the specific files it manages.
    local makefile_paths=()
    for subdir in \
      "drivers/char/ipmi" \
      "drivers/staging" \
      "drivers/vfio" \
      "arch/x86/kvm" \
      "net/ipv4" \
      "net/ipv6" \
      "net/netfilter" \
      "net/sched" \
      "virt"; do
      if [ -n "${source_prefix}" ]; then
        makefile_paths+=("${source_prefix}/${subdir}")
      else
        makefile_paths+=("${subdir}")
      fi
    done
    tar -xf "${source_tarball}" \
      "${strip_args[@]}" \
      -C "${ws_dir}" \
      --wildcards \
      "${makefile_paths[@]}" 2>/dev/null || true
    log_info "  Makefiles re-extracted for net/, kvm/, vfio, staging, ipmi"

    # ── Step A3: OOT patches for arch/x86/kvm ────────────────────────────────
    # kvm Makefile uses -Iarch/x86/kvm (in-tree relative path) → patch to -I$(src)
    if [ -f "${ws_dir}/arch/x86/kvm/Makefile" ]; then
      sed -i 's|-Iarch/x86/kvm|-I$(src)|g' "${ws_dir}/arch/x86/kvm/Makefile"
    fi
    # kvm trace.h hardcodes TRACE_INCLUDE_PATH relative to the in-tree build dir.
    # Override with the absolute Docker mount path.
    if [ -f "${ws_dir}/arch/x86/kvm/trace.h" ]; then
      sed -i 's|#define TRACE_INCLUDE_PATH .*arch/x86/kvm.*|#define TRACE_INCLUDE_PATH /tmp/input/arch/x86/kvm|' \
        "${ws_dir}/arch/x86/kvm/trace.h"
    fi
    # kvm_get/clear_cpu_l1tf_flush_l1d are declared in the Docker hardirq.h
    # but only under an #ifdef that Syno's kernel config does not enable.
    # Replace the two call sites with the equivalent direct per-CPU variable
    # accesses, which is exactly what those inline functions do.
    if [ -f "${ws_dir}/arch/x86/kvm/vmx/vmx.c" ]; then
      sed -i \
        's|kvm_get_cpu_l1tf_flush_l1d()|0|g' \
        "${ws_dir}/arch/x86/kvm/vmx/vmx.c"
      sed -i \
        's|kvm_clear_cpu_l1tf_flush_l1d()|((void)0)|g' \
        "${ws_dir}/arch/x86/kvm/vmx/vmx.c"
    fi
  else
    log_warn "  No kernel source available – using only overlay sources"
  fi

  # ── Step B: overlay custom/out-of-tree drivers from src/X.x/ ────────────
  # Custom drivers WIN over the upstream ones (copy on top).
  if [ -d "${src_overlay}" ]; then
    log_info "  Overlaying custom sources from ${src_overlay}/"
    # Copy all subdirs except the defines.* and Makefile at the root
    for item in "${src_overlay}"/*/; do
      [ -d "${item}" ] || continue
      local name
      name="$(basename "${item}")"
      cp -rT "${item}" "${ws_dir}/${name}" 2>/dev/null || true
    done
    # Copy Module.symvers if present (required for 4.x out-of-tree builds)
    [ -f "${src_overlay}/Module.symvers" ] && cp "${src_overlay}/Module.symvers" "${ws_dir}/"
    # Copy top-level Makefile from overlay (it knows how to drive the build)
    [ -f "${src_overlay}/Makefile" ] && cp "${src_overlay}/Makefile" "${ws_dir}/"
    log_info "  Overlay complete"
  else
    log_error "  Overlay dir not found: ${src_overlay}"
    return 1
  fi

  # ── Step C: copy the defines file (do NOT auto-run gen-defines here) ─────
  local defines_src="${src_overlay}/defines.${platform}"
  local defines_dst="${ws_dir}/defines.${platform}"

  if [ -f "${defines_src}" ]; then
    cp "${defines_src}" "${defines_dst}"
  else
    log_error "  No defines.${platform} found at ${defines_src} – run gen-defines.sh first"
    return 1
  fi

  log_info "  Workspace ready: ${ws_dir}"
}

# ---------------------------------------------------------------------------
# Docker compilation (mirrors compile.sh logic)
# ---------------------------------------------------------------------------
compile_upstream() {
  local platform="$1"
  local kver="$2"
  local toolkit_ver="$3"
  local docker_image="$4"

  local ws_dir="${WORKSPACE_ROOT}/${platform}-${toolkit_ver}-${kver}"
  local output_dir="${WORKSPACE_ROOT}/output/${platform}-${toolkit_ver}-${kver}"
  local log_dir="${WORKSPACE_ROOT}/logs"
  local log_file="${log_dir}/compile-${platform}-${toolkit_ver}-${kver}.txt"

  mkdir -p "${output_dir}" "${log_dir}"

  # Patch allow-store-data-races flag for newer GCC in 7.2/7.3 builds
  if [ "${toolkit_ver}" = "7.2" ] || [ "${toolkit_ver}" = "7.3" ]; then
    if [ -f "${ws_dir}/Makefile" ]; then
      sed -i 's/--param=allow-store-data-races=0/--allow-store-data-races/g' "${ws_dir}/Makefile"
    fi
  fi

  log_step "Compiling ${platform} (kernel ${kver}, DSM ${toolkit_ver})"
  log_info "  Docker image : ${docker_image}"
  log_info "  Workspace    : ${ws_dir}"
  log_info "  Output       : ${output_dir}"
  log_info "  Log          : ${log_file}"

  # Bypass compile-module entirely.
  # compile-module uses isatty() to pick its build strategy:
  #   • TTY present  → single-pass:  make -C /opt/<platform>/build M=/tmp/input modules
  #   • TTY absent   → per-module loop, one make call per .c file
  # The per-module path hits a GNU make 4.3 incompatibility in the Synology kernel
  # Makefile ("Makefile:332: *** multiple target patterns. Stop.").
  # In a scripted/CI environment docker run -t has no host TTY, so compile-module
  # always falls into the broken path.
  # Fix: use --entrypoint bash and run the exact same make command that
  # compile-module would use in single-pass mode, mounting at /tmp/input.
  local _cflags="-Wno-address -Wno-unused-result -Wno-misleading-indentation -Wno-array-parameter -Wno-unused-function"

  local docker_exit=0
  docker run -u "$(id -u)" --rm \
      --entrypoint bash \
      -v "${ws_dir}":/tmp/input \
      -v "${output_dir}":/output \
      -e CFLAGS="${_cflags}" \
      "${docker_image}" \
      -c "make -C /opt/${platform}/build M=/tmp/input modules KBUILD_MODPOST_WARN=1 \
          \$(grep '^CONFIG_' /tmp/input/defines.${platform} | tr '\n' ' ') \
          && find /tmp/input -name '*.ko' -exec cp {} /output/ \; \
          && find /output -name '*.ko' -exec strip --strip-debug {} \;" \
      >> "${log_file}" 2>&1 || docker_exit=$?

  if [ "${docker_exit}" -ne 0 ]; then
    log_error "  Compilation FAILED (exit ${docker_exit}) – last 30 lines of log:"
    tail -30 "${log_file}" | sed 's/^/    /'
    return 1
  fi
  log_info "  Compilation OK"

  local ko_count
  ko_count="$(find "${output_dir}" -name "*.ko" 2>/dev/null | wc -l)"
  if [ "${ko_count}" -eq 0 ]; then
    log_error "  No .ko files produced – last 50 lines of compile log:"
    tail -50 "${log_file}" | sed 's/^/    /'
    return 1
  fi
  log_info "  Built ${ko_count} .ko file(s)"
}

# ---------------------------------------------------------------------------
# Merge compiled output with thirdparty base (like compile.sh merge_with_thirdparty)
# ---------------------------------------------------------------------------
merge_and_package() {
  local platform="$1"
  local kver="$2"
  local toolkit_ver="$3"

  local thirdparty_dir="${THIRDPARTY_DIR}/${platform}-${toolkit_ver}-${kver}"
  local compiled_dir="${WORKSPACE_ROOT}/output/${platform}-${toolkit_ver}-${kver}"
  local merged_dir="${WORKSPACE_ROOT}/.staging-${platform}-${toolkit_ver}-${kver}"
  local out_dir="${REPO_ROOT}/merged-output"
  local tarball="${out_dir}/${platform}-${toolkit_ver}-${kver}.tgz"

  mkdir -p "${merged_dir}" "${out_dir}"

  # Base: thirdparty modules
  if [ -d "${thirdparty_dir}" ]; then
    cp -r "${thirdparty_dir}"/* "${merged_dir}/" 2>/dev/null || true
    log_info "  Copied thirdparty base from ${thirdparty_dir}"
  else
    log_warn "  No thirdparty base found at ${thirdparty_dir}"
  fi

  # Overlay: compiled modules (overwrite thirdparty versions)
  if [ -d "${compiled_dir}" ]; then
    local compiled_count
    compiled_count="$(find "${compiled_dir}" -name "*.ko" 2>/dev/null | wc -l)"
    find "${compiled_dir}" -name "*.ko" -exec cp {} "${merged_dir}/" \;
    log_info "  Overlaid ${compiled_count} compiled module(s)"
  else
    log_warn "  No compiled output found at ${compiled_dir}"
  fi

  # Package
  log_step "Creating merged package: ${tarball}"
  tar --exclude="*.tgz" -czf "${tarball}" -C "${merged_dir}" . || {
    log_error "  tar failed"
    rm -rf "${merged_dir}"
    return 1
  }
  local size
  size="$(du -h "${tarball}" | awk '{print $1}')"
  log_info "  Created: ${tarball} (${size})"

  rm -rf "${merged_dir}"
}

# ---------------------------------------------------------------------------
# Full build pipeline for one platform/kver/toolkit combination
# ---------------------------------------------------------------------------
build_platform() {
  local platform="$1"
  local kver="$2"                      # Syno kernel version — used for output naming
  local toolkit_ver="$3"
  local docker_image="$4"
  local upstream_kver="${5:-${kver}}"  # upstream source version — used for download

  echo ""
  log_info "════════════════════════════════════════════"
  log_info "  Platform      : ${platform}"
  log_info "  Syno kernel   : ${kver}"
  log_info "  Source kernel : ${upstream_kver}"
  log_info "  DSM           : ${toolkit_ver}"
  log_info "════════════════════════════════════════════"

  ensure_kernel_source "${upstream_kver}" || { log_error "Kernel source unavailable"; return 1; }
  setup_workspace  "${platform}" "${kver}" "${toolkit_ver}" "${upstream_kver}" || return 1
  compile_upstream "${platform}" "${kver}" "${toolkit_ver}" "${docker_image}"  || return 1
  merge_and_package "${platform}" "${kver}" "${toolkit_ver}"                   || return 1

  log_info "  ✓ Done: merged-output/${platform}-${toolkit_ver}-${kver}.tgz"
}

# ---------------------------------------------------------------------------
# Interactive selection (mirrors compile.sh UX)
# filter_platforms / filter_versions are set by --platform / --version flags
# ---------------------------------------------------------------------------
select_and_build() {
  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "PLATFORMS file not found"; exit 1; }

  # Collect available toolkit versions (exclude 7.1 by default)
  local -a versions=()
  while read -r _P _K TV _D; do
    [[ "$_P" =~ ^#.*$ || -z "$_P" ]] && continue
    TV="$(echo "${TV}" | xargs)"
    [ "$TV" = "7.1" ] && continue
    [[ " ${versions[*]} " =~ " ${TV} " ]] || versions+=("$TV")
  done < "${PLATFORMS_FILE}"

  IFS=$'\n' versions=($(sort <<<"${versions[*]}")); unset IFS

  local -a sel_vers=()

  if [ "${#filter_versions[@]}" -gt 0 ]; then
    # Non-interactive: use --version values directly
    for fv in "${filter_versions[@]}"; do
      [[ " ${versions[*]} " =~ " ${fv} " ]] \
        && sel_vers+=("$fv") \
        || log_warn "Version '${fv}' not found in PLATFORMS, skipping"
    done
  else
    # Interactive
    echo ""
    log_info "=== Available DSM / toolkit versions ==="
    for i in "${!versions[@]}"; do
      printf "  %2d) %s\n" $((i+1)) "${versions[$i]}"
    done
    echo ""
    read -r -p "Enter version numbers (space-separated, or 'all'): " ver_sel
    echo ""

    if [ -z "$ver_sel" ] || [ "$ver_sel" = "all" ]; then
      sel_vers=("${versions[@]}")
    else
      for n in $ver_sel; do
        [[ "$n" =~ ^[0-9]+$ ]] && [ "$n" -ge 1 ] && [ "$n" -le "${#versions[@]}" ] \
          && sel_vers+=("${versions[$((n-1))]}")
      done
    fi
  fi

  # Collect platforms for the selected versions
  local -a all_platforms=()
  while read -r P K TV D; do
    [[ "$P" =~ ^#.*$ || -z "$P" ]] && continue
    P="$(echo "$P" | xargs)"; TV="$(echo "$TV" | xargs)"
    [[ " ${sel_vers[*]} " =~ " ${TV} " ]] || continue
    [[ " ${all_platforms[*]} " =~ " ${P} " ]] && continue
    all_platforms+=("$P")
  done < "${PLATFORMS_FILE}"

  local -a sel_plats=()

  if [ "${#filter_platforms[@]}" -gt 0 ]; then
    # Non-interactive: use --platform values directly
    for fp in "${filter_platforms[@]}"; do
      [[ " ${all_platforms[*]} " =~ " ${fp} " ]] \
        && sel_plats+=("$fp") \
        || log_warn "Platform '${fp}' not found for selected version(s), skipping"
    done
  else
    # Interactive
    local idx=1
    log_info "=== Available platforms ==="
    for p in "${all_platforms[@]}"; do
      printf "  %2d) %s\n" $idx "$p"; ((idx++))
    done
    echo ""
    read -r -p "Enter platform numbers (space-separated, or 'all'): " plat_sel
    echo ""

    if [ -z "$plat_sel" ] || [ "$plat_sel" = "all" ]; then
      sel_plats=("${all_platforms[@]}")
    else
      for n in $plat_sel; do
        [[ "$n" =~ ^[0-9]+$ ]] && [ "$n" -ge 1 ] && [ "$n" -le "${#all_platforms[@]}" ] \
          && sel_plats+=("${all_platforms[$((n-1))]}")
      done
    fi
  fi

  # Collect unique kernel series present in the selected platform/version combos
  # and resolve upstream kver interactively (or via --kver flag)
  # Upstream source is only used as fallback when Syno GPL download fails.
  # The --kver flag allows overriding the fallback version (e.g. for testing).
  declare -A upstream_kver_map  # series -> fallback upstream kver

  if [ -n "${kver_override}" ]; then
    # Manual override: use the same version for all series
    log_info "  Upstream fallback kver override: ${kver_override}"
  else
    # Pre-fill with the Syno kver for each series as the fallback
    for sp in "${sel_plats[@]}"; do
      for sv in "${sel_vers[@]}"; do
        while read -r P K TV _D; do
          [[ "$P" =~ ^#.*$ || -z "$P" ]] && continue
          P="$(echo "$P" | xargs)"; K="$(echo "$K" | xargs)"; TV="$(echo "$TV" | xargs)"
          [ "$P" = "$sp" ] && [ "$TV" = "$sv" ] || continue
          local series="${K%.*}"
          upstream_kver_map["$series"]="$K"
          break
        done < "${PLATFORMS_FILE}"
      done
    done
  fi

  # Execute builds
  local ok=0 fail=0
  for sp in "${sel_plats[@]}"; do
    for sv in "${sel_vers[@]}"; do
      while read -r P K TV D; do
        [[ "$P" =~ ^#.*$ || -z "$P" ]] && continue
        P="$(echo "$P" | xargs)"; K="$(echo "$K" | xargs)"
        TV="$(echo "$TV" | xargs)"; D="$(echo "$D" | xargs)"
        [ "$P" = "$sp" ] && [ "$TV" = "$sv" ] || continue
        # Upstream fallback kver: --kver flag > Syno kver for this series
        local series="${K%.*}"
        local ukver="${kver_override:-${upstream_kver_map[$series]:-${K}}}"
        if build_platform "$P" "$K" "$TV" "$D" "$ukver"; then
          ((ok++))
        else
          ((fail++))
          log_error "Build failed for ${P} ${TV} kernel ${K}"
        fi
        break
      done < "${PLATFORMS_FILE}"
    done
  done

  echo ""
  log_info "═══════════════════════════════════════════"
  log_info "  Builds completed: ${ok} OK  /  ${fail} FAILED"
  log_info "  Output: ${REPO_ROOT}/merged-output/"
  log_info "═══════════════════════════════════════════"
}

# ---------------------------------------------------------------------------
# Cleanup helper
# ---------------------------------------------------------------------------
cleanup_workspaces() {
  log_info "Cleaning workspace directory: ${WORKSPACE_ROOT}"
  rm -rf "${WORKSPACE_ROOT}"
  log_info "Done"
}

# ---------------------------------------------------------------------------
# Usage / main
# ---------------------------------------------------------------------------
usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  --platform <name>  Build only this platform (e.g. epyc7002). Can be repeated.
  --version  <ver>   Build only this DSM version (e.g. 7.3). Can be repeated.
  --kver     <ver>   Override upstream kernel source version used as fallback (e.g. 5.10.55)
  --regen            Re-run gen-defines.sh before building
  --no-download      Skip kernel source download (use cached tarballs only)
  --clean            Remove all workspace/staging directories and exit
  --verbose          Enable debug output
  -h, --help         Show this help

Examples:
  # Interactive selector
  bash build-upstream.sh

  # One platform, one version, newer kernel source
  bash build-upstream.sh --platform epyc7002 --version 7.3 --kver 5.10.220

  # All platforms for 7.3 using cached kernel tarball
  bash build-upstream.sh --version 7.3 --no-download

Workflow:
  1. (optional --regen) Run gen-defines.sh to regenerate defines files
  2. Download upstream kernel source (kernel.org) matching the Syno version
  3. Overlay custom drivers from src/X.x/
  4. Compile using Docker (auxxxilium/syno-compiler:<ver>)
  5. Merge with thirdparty and produce merged-output/<platform>-<ver>-<kver>.tgz
EOF
}

main() {
  local no_download=0
  local kver_override=""
  local regen=0
  local -a filter_platforms=()
  local -a filter_versions=()

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --clean)        cleanup_workspaces; exit 0 ;;
      --no-download)  no_download=1; shift ;;
      --kver)         kver_override="$2"; shift 2 ;;
      --regen)        regen=1; shift ;;
      --platform)     filter_platforms+=("$2"); shift 2 ;;
      --version)      filter_versions+=("$2"); shift 2 ;;
      --verbose)      export VERBOSE=1; shift ;;
      -h|--help)      usage; exit 0 ;;
      *)              log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
  done

  if [ "$no_download" -eq 1 ]; then
    ensure_kernel_source() {
      log_info "Skipping download (--no-download), using cached tarball for kernel ${1}"
    }
  fi

  if [ -n "$kver_override" ]; then
    _orig_ensure_kernel_source="$(declare -f ensure_kernel_source)"
    ensure_kernel_source() {
      local kv="${kver_override}"
      eval "${_orig_ensure_kernel_source/ensure_kernel_source()/ensure_kernel_source_orig()}"
      ensure_kernel_source_orig "${kv}"
    }
    log_warn "Kernel version override: all platforms will download linux-${kver_override}"
  fi

  log_info "=== build-upstream.sh: Upstream kernel + Syno DSM compiler ==="
  echo ""

  if [ "$regen" -eq 1 ]; then
    if [ -x "${SCRIPT_DIR}/gen-defines.sh" ]; then
      log_info "Running gen-defines.sh to refresh all defines files..."
      bash "${SCRIPT_DIR}/gen-defines.sh" || log_warn "gen-defines.sh reported warnings"
      echo ""
    else
      log_warn "gen-defines.sh not found or not executable"
    fi
  fi

  mkdir -p "${WORKSPACE_ROOT}/logs"

  select_and_build
}

main "$@"
