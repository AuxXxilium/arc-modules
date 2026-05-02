#!/usr/bin/env bash

set -e
set -o pipefail

# Color output for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
  echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
  echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
  echo -e "${RED}[ERROR]${NC} $*"
}

# Function to compile modules for a given platform and kernel version
compile_modules() {
  local PLATFORM=$1
  local KVER=$2
  local TOOLKIT_VER=$3
  local DOCKER_IMAGE=$4
  local SKIP_MERGE=${5:-0}  # Optional: skip merge if set to 1 (for storage-only mode)

  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && return

  # Check if the defines.<platform> file exists
  DEFINES_FILE="${PWD}/${DIR}/defines.${PLATFORM}"
  if [ ! -f "${DEFINES_FILE}" ]; then
    log_error "Error: ${DEFINES_FILE} not found for platform ${PLATFORM}."
    return 1
  fi

  # Create output directory for Docker
  local OUTPUT_DIR="${PWD}/output/${PLATFORM}-${KVER}"
  mkdir -p "${OUTPUT_DIR}"

  # Create logs directory
  mkdir -p "${PWD}/logs"
  local LOG_FILE="${PWD}/logs/compile-${PLATFORM}-${TOOLKIT_VER}-${KVER}.txt"

  # Handle allow-store-data-races flag for kernel versions 7.2 and 7.3
  if [ "$TOOLKIT_VER" = "7.2" ] || [ "$TOOLKIT_VER" = "7.3" ]; then
    sed -i 's/--param=allow-store-data-races=0/--allow-store-data-races/g' "${PWD}/${DIR}/Makefile"
  fi

  # Run the Docker container with compiler warning suppression
  log_info "Starting Docker compilation for ${PLATFORM} (kernel ${KVER})"
  log_info "Build log: ${LOG_FILE}"
  if docker run -u $(id -u) --rm -t -v "${PWD}/${DIR}":/input -v "${OUTPUT_DIR}":/output \
    -e CFLAGS="-Wno-address -Wno-unused-result -Wno-misleading-indentation -Wno-array-parameter -Wno-unused-function" \
    ${DOCKER_IMAGE} compile-module "${PLATFORM}" >> "${LOG_FILE}" 2>&1; then
    log_info "Docker compilation completed successfully"
  else
    log_error "Docker compilation failed"
    return 1
  fi
  
  if [ ! -d "${OUTPUT_DIR}" ]; then
    log_error "Output directory ${OUTPUT_DIR} does not exist"
    return 1
  fi

  local FILE_COUNT=$(ls -1 "${OUTPUT_DIR}" 2>/dev/null | wc -l)
  if [ "$FILE_COUNT" -eq 0 ]; then
    log_error "No files in ${OUTPUT_DIR}"
    return 1
  fi

  log_info "Found $FILE_COUNT file(s) in ${OUTPUT_DIR}"

  # Handle output directory naming and packaging (skip if SKIP_MERGE is 1 for storage-only mode)
  if [ "${SKIP_MERGE}" -ne 1 ]; then
    PACKAGE_NAME="${PLATFORM}-${TOOLKIT_VER}-${KVER}.tgz"
    local TARBALL_PATH="${PWD}/output/${PACKAGE_NAME}"
    
    log_info "Creating tarball: ${TARBALL_PATH}"
    if tar --exclude="*.tgz" -czf "${TARBALL_PATH}" -C "${OUTPUT_DIR}" .; then
      if [ -f "${TARBALL_PATH}" ]; then
        local SIZE=$(du -h "${TARBALL_PATH}" | awk '{print $1}')
        log_info "✓ Successfully created: ${TARBALL_PATH} (${SIZE})"
      else
        log_error "✗ Tarball not found after creation"
        return 1
      fi
    else
      log_error "✗ Failed to create tarball"
      return 1
    fi
  fi

  # Merge with thirdparty modules and create final package (skip if SKIP_MERGE is 1)
  if [ "${SKIP_MERGE}" -ne 1 ]; then
    merge_with_thirdparty "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}" ""
  fi
}

# Function to check if a module is storage-related (HBA/SAS/SCSI/RAID)
# Takes platform and kernel version to check platform-specific configs
# Returns 0 if module should be included, 1 if excluded
is_storage_module() {
  local module_name=$1
  local platform=$2
  local kver=$3
  
  # All potential storage modules
  if ! [[ "$module_name" =~ ^(libsas|mpt3sas|mptbase|mptctl|mptsas|mptscsih|mptspi|scsi_transport_sas|scsi_transport_spi|sr_mod|hpsa|aacraid|mvsas|3w|aic)$ ]]; then
    return 1  # Not a storage module
  fi
  
  # If no platform context provided, include all storage modules
  if [ -z "$platform" ] || [ -z "$kver" ]; then
    return 0
  fi
  
  local dir="${kver:0:1}.x"
  local defines_file="${PWD}/${dir}/defines.${platform}"
  
  if [ ! -f "$defines_file" ]; then
    # If defines file doesn't exist, include the module by default
    return 0
  fi
  
  # Check each module's CONFIG flag to see if it's built-in (=y)
  # If built-in, exclude it (return 1). If modular (=m) or not set, include it (return 0)
  local config_flag=""
  case "$module_name" in
    libsas)
      config_flag="CONFIG_SCSI_SAS_LIBSAS"
      ;;
    mpt3sas)
      config_flag="CONFIG_SCSI_MPT3SAS"
      ;;
    mptbase|mptscsih)
      # mptbase and mptscsih depend on CONFIG_FUSION
      config_flag="CONFIG_FUSION"
      ;;
    mptctl)
      config_flag="CONFIG_FUSION_CTL"
      ;;
    mptsas)
      config_flag="CONFIG_FUSION_SAS"
      ;;
    mptspi)
      config_flag="CONFIG_FUSION_SPI"
      ;;
    scsi_transport_sas)
      config_flag="CONFIG_SCSI_TRANSPORT_SAS"
      ;;
    scsi_transport_spi)
      config_flag="CONFIG_SCSI_TRANSPORT_SPI"
      ;;
    sr_mod)
      config_flag="CONFIG_BLK_DEV_SR"
      ;;
    hpsa)
      config_flag="CONFIG_SCSI_HPSA"
      ;;
    aacraid)
      config_flag="CONFIG_SCSI_AACRAID"
      ;;
    mvsas)
      config_flag="CONFIG_SCSI_MVSAS"
      ;;
    3w)
      config_flag="CONFIG_SCSI_3W_9XXX"
      ;;
    aic)
      config_flag="CONFIG_SCSI_AIC94XX"
      ;;
  esac
  
  # Check if the CONFIG flag is built-in (=y), modular (=m), or not set
  # - If =y (built-in) → exclude (return 1)
  # - If =m (modular) → include (return 0)
  # - If not set → exclude (return 1), module wasn't compiled
  if grep -q "^${config_flag}=y$" "$defines_file"; then
    log_warn "    ⊘ ${module_name}.ko (CONFIG_${config_flag#CONFIG_}=y, built-in)"
    return 1  # Built-in, so exclude
  elif grep -q "^${config_flag}=m$" "$defines_file"; then
    return 0  # Modular, so include
  else
    log_warn "    ⊘ ${module_name}.ko (${config_flag} not set)"
    return 1  # Not configured, so module wasn't compiled - exclude
  fi
}

# Function to merge compiled modules with thirdparty base and create final package
merge_with_thirdparty() {
  local PLATFORM=$1
  local KVER=$2
  local TOOLKIT_VER=$3
  local MODULE_TYPE=${4:-""}  # Optional: "movbe", "storage-only" or empty

  # Get repository root (parent of src directory)
  local REPO_ROOT="$(dirname "${PWD}")"
  local THIRDPARTY_DIR="${REPO_ROOT}/thirdparty"
  local THIRDPARTY_PLATFORM_DIR="${THIRDPARTY_DIR}/${PLATFORM}-${TOOLKIT_VER}-${KVER}"

  # Check if thirdparty directory exists for this platform-version combo
  if [ ! -d "${THIRDPARTY_PLATFORM_DIR}" ]; then
    log_warn "No thirdparty directory found at ${THIRDPARTY_PLATFORM_DIR}, skipping merge"
    return 0
  fi

  # Create merged-output directory
  local MERGED_OUTPUT_ROOT="${PWD}/merged-output"
  local SUFFIX=""
  [ -n "${MODULE_TYPE}" ] && SUFFIX="-${MODULE_TYPE}"
  local MERGED_STAGING_DIR="${MERGED_OUTPUT_ROOT}/.staging-${PLATFORM}-${KVER}${SUFFIX}"
  mkdir -p "${MERGED_STAGING_DIR}"

  log_info "Merging compiled modules${SUFFIX:+ ($MODULE_TYPE)} with thirdparty base..."
  log_info "  Thirdparty source: ${THIRDPARTY_PLATFORM_DIR}"

  # Step 1: Copy thirdparty modules to staging area
  if [ "${MODULE_TYPE}" = "storage-only" ]; then
    # For storage-only: copy ALL thirdparty modules (we'll overlay with compiled storage only)
    if cp -r "${THIRDPARTY_PLATFORM_DIR}"/* "${MERGED_STAGING_DIR}/" >/dev/null 2>&1; then
      log_info "  ✓ Copied all thirdparty base modules"
    else
      log_warn "  ⚠ Could not copy thirdparty modules (directory may be empty)"
    fi
  else
    # For standard/movbe, copy all thirdparty modules
    if cp -r "${THIRDPARTY_PLATFORM_DIR}"/* "${MERGED_STAGING_DIR}/" >/dev/null 2>&1; then
      log_info "  ✓ Copied thirdparty base modules"
    else
      log_warn "  ⚠ Could not copy thirdparty modules (directory may be empty)"
    fi
  fi
  
  # Step 2: Copy compiled .ko files, overwriting thirdparty versions
  local COMPILED_OUTPUT_DIR="${PWD}/output/${PLATFORM}-${KVER}"
  if [ -d "${COMPILED_OUTPUT_DIR}" ]; then
    local COMPILED_COUNT=$(find "${COMPILED_OUTPUT_DIR}" -type f -name "*.ko" 2>/dev/null | wc -l)
    if [ "$COMPILED_COUNT" -gt 0 ]; then
      # If storage-only filter is requested, only copy storage-related modules
      if [ "${MODULE_TYPE}" = "storage-only" ]; then
        # Use temporary file to handle subshell issues with variable updates
        local tmp_ko_list=$(mktemp)
        find "${COMPILED_OUTPUT_DIR}" -type f -name "*.ko" > "$tmp_ko_list" 2>&1 || true
        
        local storage_count=0
        set +e  # Disable exit-on-error for this loop
        while IFS= read -r ko_file; do
          if [ -z "$ko_file" ]; then continue; fi
          local basename=$(basename "$ko_file" .ko)
          if is_storage_module "$basename" "$PLATFORM" "$KVER" >/dev/null 2>&1; then
            cp "$ko_file" "${MERGED_STAGING_DIR}/" >/dev/null 2>&1 && ((storage_count++))
          fi
        done < "$tmp_ko_list"
        set -e  # Re-enable exit-on-error
        rm -f "$tmp_ko_list"
        
        log_info "  ✓ Merged $storage_count storage-related compiled module(s) (replacing thirdparty versions)"
      else
        find "${COMPILED_OUTPUT_DIR}" -type f -name "*.ko" -exec cp {} "${MERGED_STAGING_DIR}/" \; >/dev/null 2>&1 || true
        log_info "  ✓ Merged $COMPILED_COUNT compiled module(s) (replacing thirdparty versions)"
      fi
    else
      log_warn "  ⚠ No .ko files found in ${COMPILED_OUTPUT_DIR}"
    fi
  else
    log_warn "  ⚠ Compiled output directory not found: ${COMPILED_OUTPUT_DIR}"
  fi
  
  # Step 3: Create final tarball in merged-output
  local MERGED_PACKAGE_NAME="${PLATFORM}-${TOOLKIT_VER}-${KVER}.tgz"
  local MERGED_TARBALL_PATH="${MERGED_OUTPUT_ROOT}/${MERGED_PACKAGE_NAME}"

  log_info "Creating merged package: ${MERGED_TARBALL_PATH}"
  
  if tar --exclude="*.tgz" -czf "${MERGED_TARBALL_PATH}" -C "${MERGED_STAGING_DIR}" .; then
    if [ -f "${MERGED_TARBALL_PATH}" ]; then
      local SIZE=$(du -h "${MERGED_TARBALL_PATH}" | awk '{print $1}')
      log_info "✓ Successfully created merged package: ${MERGED_TARBALL_PATH} (${SIZE})"
    else
      log_error "✗ Merged tarball not found after creation"
      rm -rf "${MERGED_STAGING_DIR}"
      return 1
    fi
  else
    log_error "✗ Failed to create merged tarball (tar exited with error)"
    rm -rf "${MERGED_STAGING_DIR}"
    return 1
  fi

  # Cleanup staging directory
  rm -rf "${MERGED_STAGING_DIR}"
}

# Function to compile MOVBE module for a given platform and kernel version
compile_movbe_module() {
  local PLATFORM=$1
  local KVER=$2
  local TOOLKIT_VER=$3
  local DOCKER_IMAGE=$4

  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && return

  # Check if the defines.<platform> file exists
  DEFINES_FILE="${PWD}/${DIR}/defines.${PLATFORM}"
  if [ ! -f "${DEFINES_FILE}" ]; then
    log_error "Error: ${DEFINES_FILE} not found for platform ${PLATFORM}."
    return 1
  fi

  # Check if MOVBE module exists
  if [ ! -d "${PWD}/movbe" ]; then
    log_error "Error: MOVBE module directory not found at ${PWD}/movbe"
    return 1
  fi

  # Check if MOVBE module exists
  if [ ! -d "${PWD}/movbe" ] || [ ! -f "${PWD}/movbe/Makefile" ]; then
    log_error "Error: MOVBE module directory or Makefile not found at ${PWD}/movbe"
    return 1
  fi

  # Create a temporary directory to stage MOVBE with platform-specific defines
  local TEMP_MOVBE_INPUT="${PWD}/.movbe-input-${PLATFORM}-${KVER}"
  mkdir -p "${TEMP_MOVBE_INPUT}"
  
  log_info "Staging MOVBE module for ${PLATFORM}..."
  
  # Copy MOVBE source files
  cp "${PWD}/movbe"/*.c "${TEMP_MOVBE_INPUT}/" 2>/dev/null || true
  cp "${PWD}/movbe"/Makefile "${TEMP_MOVBE_INPUT}/" 2>/dev/null || true
  
  # Create platform-specific defines file by combining platform config with MOVBE config
  {
    # Extract the first CONFIG_* line (platform identifier) from the original defines file
    head -n 1 "${DEFINES_FILE}"
    # Append the rest of the MOVBE configuration
    cat "${PWD}/movbe/defines.movbe"
  } > "${TEMP_MOVBE_INPUT}/defines.${PLATFORM}"
  
  # Create output directory for Docker
  local OUTPUT_DIR="${PWD}/output/${PLATFORM}-${KVER}-movbe"
  mkdir -p "${OUTPUT_DIR}"

  # Create logs directory
  mkdir -p "${PWD}/logs"
  local LOG_FILE="${PWD}/logs/compile-${PLATFORM}-${TOOLKIT_VER}-${KVER}-movbe.txt"

  # Run the Docker container using the proper compile-module function from do.sh
  log_info "Starting Docker MOVBE module compilation for ${PLATFORM} (kernel ${KVER})"
  log_info "Build log: ${LOG_FILE}"
  if docker run -u $(id -u) --rm -t -v "${TEMP_MOVBE_INPUT}":/input -v "${OUTPUT_DIR}":/output \
    -e CFLAGS="-Wno-address -Wno-unused-result -Wno-misleading-indentation -Wno-array-parameter -Wno-unused-function" \
    ${DOCKER_IMAGE} compile-module "${PLATFORM}" 2>&1 | tee -a "${LOG_FILE}"; then
    log_info "Docker MOVBE module compilation completed successfully"
  else
    log_error "Docker MOVBE module compilation failed"
    # Clean up temporary input directory
    rm -rf "${TEMP_MOVBE_INPUT}"
    return 1
  fi

  # Clean up temporary input directory
  rm -rf "${TEMP_MOVBE_INPUT}"
  
  if [ ! -d "${OUTPUT_DIR}" ]; then
    log_error "Output directory ${OUTPUT_DIR} does not exist"
    return 1
  fi

  local FILE_COUNT=$(ls -1 "${OUTPUT_DIR}" 2>/dev/null | wc -l)
  if [ "$FILE_COUNT" -eq 0 ]; then
    log_error "No .ko files in ${OUTPUT_DIR}"
    return 1
  fi

  log_info "Found $FILE_COUNT file(s) in ${OUTPUT_DIR}"

  # Handle output directory naming and packaging
  PACKAGE_NAME="${PLATFORM}-${TOOLKIT_VER}-${KVER}-movbe.tgz"
  local TARBALL_PATH="${PWD}/output/${PACKAGE_NAME}"
  
  log_info "Creating tarball: ${TARBALL_PATH}"
  if tar --exclude="*.tgz" -czf "${TARBALL_PATH}" -C "${OUTPUT_DIR}" .; then
    if [ -f "${TARBALL_PATH}" ]; then
      local SIZE=$(du -h "${TARBALL_PATH}" | awk '{print $1}')
      log_info "✓ Successfully created: ${TARBALL_PATH} (${SIZE})"
    else
      log_error "✗ Tarball not found after creation"
      return 1
    fi
  else
    log_error "✗ Failed to create tarball"
    return 1
  fi

  # Merge with thirdparty modules and create final package (for MOVBE)
  merge_with_thirdparty "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}" "movbe"
}

compile_binary() {
  local PLATFORM=$1
  local BUILD_SCRIPT=$2
  local DOCKER_IMAGE=$3

  # Check if version directory exists (use major version for output dir)
  local DIR_PATTERN=$(echo "${BUILD_SCRIPT}" | grep -oE '[0-9]+\.[0-9]+' | head -1 || echo "")
  local DIR="${DIR_PATTERN:0:1}.x"
  
  if [ -z "$DIR" ]; then
    log_error "Error: Could not determine version directory from build script name"
    return 1
  fi
  
  [ ! -d "${PWD}/${DIR}" ] && return

  # Check if build script exists
  BUILD_FILE="${PWD}/${DIR}/${BUILD_SCRIPT}"
  if [ ! -f "${BUILD_FILE}" ]; then
    log_error "Error: ${BUILD_FILE} not found for platform ${PLATFORM}."
    return 1
  fi

  # Create output directory for Docker
  local OUTPUT_DIR="${PWD}/output/${PLATFORM}-binary"
  mkdir -p "${OUTPUT_DIR}"

  # Create logs directory
  mkdir -p "${PWD}/logs"
  local LOG_FILE="${PWD}/logs/compile-${PLATFORM}-binary.txt"

  # Run the Docker container
  log_info "Starting binary compilation for ${PLATFORM} using ${BUILD_SCRIPT}"
  log_info "Build log: ${LOG_FILE}"
  if docker run --privileged -u $(id -u) --rm -t -v "${PWD}/${DIR}":/input -v "${OUTPUT_DIR}":/output \
    -e CFLAGS="-Wno-address -Wno-unused-result -Wno-misleading-indentation -Wno-array-parameter -Wno-unused-function" \
    ${DOCKER_IMAGE} compile-binary "${PLATFORM}" "${BUILD_SCRIPT}" 2>&1 | tee -a "${LOG_FILE}"; then
    log_info "Docker binary compilation completed successfully"
  else
    log_error "Docker binary compilation failed"
    return 1
  fi

  local FILE_COUNT=$(ls -1 "${OUTPUT_DIR}" 2>/dev/null | wc -l)
  if [ "$FILE_COUNT" -eq 0 ]; then
    log_error "No files in ${OUTPUT_DIR}"
    return 1
  fi

  log_info "Found $FILE_COUNT file(s) in ${OUTPUT_DIR}"

  # Create tarball with platform name only
  local PACKAGE_NAME="${PLATFORM}-binary.tgz"
  local TARBALL_PATH="${PWD}/output/${PACKAGE_NAME}"
  
  log_info "Creating tarball: ${TARBALL_PATH}"
  if tar --exclude="*.tgz" -czf "${TARBALL_PATH}" -C "${OUTPUT_DIR}" .; then
    if [ -f "${TARBALL_PATH}" ]; then
      local SIZE=$(du -h "${TARBALL_PATH}" | awk '{print $1}')
      log_info "✓ Successfully created: ${TARBALL_PATH} (${SIZE})"
    else
      log_error "✗ Tarball not found after creation"
      return 1
    fi
  else
    log_error "✗ Failed to create tarball"
    return 1
  fi
}

# Function to select platforms and versions, then compile
# Parameters: compile_mode (standard|movbe|storage-only)
select_and_compile() {
  local compile_mode=$1
  local mode_label=""
  
  case "$compile_mode" in
    movbe)
      mode_label="MOVBE Module"
      ;;
    storage-only)
      mode_label="Storage Modules Only"
      ;;
    *)
      mode_label="Standard"
      compile_mode="standard"
      ;;
  esac

  local PLATFORMS_FILE="PLATFORMS"
  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "${PLATFORMS_FILE} not found."; exit 1; }

  # Extract unique toolkit versions (exclude 7.1)
  local -a versions=()
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
    [ "$TOOLKIT_VER" = "7.1" ] && continue
    if [[ ! " ${versions[@]} " =~ " ${TOOLKIT_VER} " ]]; then
      versions+=("$TOOLKIT_VER")
    fi
  done < "${PLATFORMS_FILE}"
  
  IFS=$'\n' versions=($(sort <<<"${versions[*]}"))
  unset IFS

  log_info "=== Available DSM/Toolkit Versions ($mode_label) ==="
  echo ""
  for i in "${!versions[@]}"; do
    printf "%2d) %s\n" $((i+1)) "${versions[$i]}"
  done
  
  echo ""
  read -r -p "Enter version numbers (space-separated, or 'all' for all): " version_selection
  echo ""
  
  local -a selected_versions=()
  if [ -z "$version_selection" ] || [ "$version_selection" = "all" ]; then
    selected_versions=("${versions[@]}")
  else
    for num in $version_selection; do
      if [[ "$num" =~ ^[0-9]+$ ]] && [ "$num" -ge 1 ] && [ "$num" -le "${#versions[@]}" ]; then
        selected_versions+=("${versions[$((num-1))]}")
      else
        log_warn "Invalid version number $num"
      fi
    done
  fi
  
  # Build unique platforms for selected versions
  log_info "=== Available Platforms ($mode_label) ==="
  echo ""
  
  local -a all_platforms=()
  local idx=1
  
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
    
    local skip=1
    for sel_ver in "${selected_versions[@]}"; do
      [ "$TOOLKIT_VER" = "$sel_ver" ] && { skip=0; break; }
    done
    [ $skip -eq 1 ] && continue
    
    PLATFORM=$(echo "${PLATFORM}" | xargs)
    
    local platform_exists=0
    for existing in "${all_platforms[@]}"; do
      [ "$existing" = "$PLATFORM" ] && { platform_exists=1; break; }
    done
    
    if [ $platform_exists -eq 0 ]; then
      all_platforms+=("$PLATFORM")
      printf "%2d) %s\n" "$idx" "$PLATFORM"
      idx=$((idx + 1))
    fi
  done < "${PLATFORMS_FILE}"
  
  echo ""
  read -r -p "Enter platform numbers (space-separated, or 'all' for all): " platform_selection
  echo ""
  
  local -a selected_platforms=()
  if [ -z "$platform_selection" ] || [ "$platform_selection" = "all" ]; then
    selected_platforms=("${all_platforms[@]}")
  else
    for num in $platform_selection; do
      if [[ "$num" =~ ^[0-9]+$ ]] && [ "$num" -ge 1 ] && [ "$num" -le "${#all_platforms[@]}" ]; then
        selected_platforms+=("${all_platforms[$((num-1))]}")
      else
        log_warn "Invalid platform number $num"
      fi
    done
  fi

  # Compile all selected combinations
  for SELECTED_PLATFORM in "${selected_platforms[@]}"; do
    for SELECTED_VERSION in "${selected_versions[@]}"; do
      while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
        [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
        PLATFORM=$(echo "${PLATFORM}" | xargs)
        TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
        KVER=$(echo "${KVER}" | xargs)
        DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
        
        if [ "$PLATFORM" = "$SELECTED_PLATFORM" ] && [ "$TOOLKIT_VER" = "$SELECTED_VERSION" ]; then
          case "$compile_mode" in
            movbe)
              compile_movbe_module "$PLATFORM" "$KVER" "$TOOLKIT_VER" "$DOCKER_IMAGE"
              ;;
            storage-only)
              compile_modules "$PLATFORM" "$KVER" "$TOOLKIT_VER" "$DOCKER_IMAGE" 1
              merge_with_thirdparty "$PLATFORM" "$KVER" "$TOOLKIT_VER" "storage-only"
              ;;
            *)
              compile_modules "$PLATFORM" "$KVER" "$TOOLKIT_VER" "$DOCKER_IMAGE"
              ;;
          esac
          break
        fi
      done < "${PLATFORMS_FILE}"
    done
  done
}


main() {
  log_info "=== Module Compiler (Docker) ==="
  echo ""

  if [ -d "${PWD}/logs" ]; then
    log_info "Cleaning old logs..."
    rm -rf "${PWD}/logs"
  fi
  mkdir -p "${PWD}/logs"
  log_info "Build logs will be saved to: ${PWD}/logs"
  echo ""

  PLATFORMS_FILE="PLATFORMS"
  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "${PLATFORMS_FILE} not found."; exit 1; }

  log_info "=== Compilation Mode ==="
  echo ""
  echo "1) Compile standard modules"
  echo "2) Compile MOVBE module"
  echo "3) Compile storage modules"
  echo ""
  read -r -p "Select compilation mode: " compile_mode
  echo ""
  
  case "$compile_mode" in
    2)
      select_and_compile "movbe"
      ;;
    3)
      select_and_compile "storage-only"
      ;;
    1|*)
      if [ -n "$1" ]; then
        log_info "Compiling modules for platform: $1"
        while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
          [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
          PLATFORM=$(echo "${PLATFORM}" | xargs)
          [ "$(echo "${PLATFORM}" | tr '[:upper:]' '[:lower:]')" != "$(echo "$1" | tr '[:upper:]' '[:lower:]')" ] && continue
          KVER=$(echo "${KVER}" | xargs)
          TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
          DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
          compile_modules "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}" "${DOCKER_IMAGE}"
        done < "${PLATFORMS_FILE}"
      else
        select_and_compile "standard"
      fi
      ;;
  esac
}

# Run the main function
main "$@"