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

# Function to check if a module depends on scsi_transport_sas (directly or indirectly)
# These modules will replace thirdparty versions during merge if they exist
# Returns 0 if module should be included, 1 if excluded
is_storage_module() {
  local module_name=$1
  local platform=$2
  local kver=$3
  
  # All modules in the scsi_transport_sas dependency chain (direct and indirect)
  # - scsi_transport_sas: the base SAS transport layer
  # - scsi_transport_spi: the SPI transport layer (needed by mptspi)
  # - Direct consumers: mpt3sas, libsas
  # - MPT Fusion family (mptsas depends on mptbase+mptscsih, shared with other MPT modules):
  #   mptbase, mptscsih, mptsas, mptctl, mptspi
  # - Other SAS controllers: hpsa, smartpqi, sd_mod, ses
  # - Indirect (via libsas): aic94xx, mvsas
  if [[ "$module_name" =~ ^(scsi_transport_sas|scsi_transport_spi|libsas|mpt3sas|mptbase|mptscsih|mptsas|mptctl|mptspi|hpsa|smartpqi|sd_mod|ses|aic94xx|mvsas)$ ]]; then
    return 0  # Module is in the list, include it for replacement
  fi
  
  return 1  # Not in scsi_transport_sas dependency chain
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
    # For storage-only: copy ALL thirdparty modules (we'll overlay with compiled SAS/SCSI storage only)
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
      # If storage-only filter is requested, only copy SAS/SCSI storage-related modules
      if [ "${MODULE_TYPE}" = "storage-only" ]; then
        # Use temporary file to handle subshell issues with variable updates
        local tmp_ko_list=$(mktemp)
        find "${COMPILED_OUTPUT_DIR}" -type f -name "*.ko" > "$tmp_ko_list" 2>&1 || true
        
        local storage_count=0
        local replaced_count=0
        local added_count=0
        set +e  # Disable exit-on-error for this loop
        while IFS= read -r ko_file; do
          if [ -z "$ko_file" ]; then continue; fi
          local basename=$(basename "$ko_file" .ko)
          
          # Always add movbe_emulator module (for 7.3 builds)
          if [ "$basename" = "movbe_emulator" ]; then
            cp "$ko_file" "${MERGED_STAGING_DIR}/" >/dev/null 2>&1 && {
              log_info "    + Adding movbe_emulator.ko"
              ((added_count++))
            }
            continue
          fi
          
          if is_storage_module "$basename" "$PLATFORM" "$KVER" >/dev/null 2>&1; then
            # Only replace if this module exists in thirdparty (staging dir already has thirdparty modules)
            if [ -f "${MERGED_STAGING_DIR}/$(basename "$ko_file")" ]; then
              log_info "    ↻ Replacing thirdparty $(basename "$ko_file") with compiled version"
              cp "$ko_file" "${MERGED_STAGING_DIR}/" >/dev/null 2>&1 && ((replaced_count++))
            fi
          fi
        done < "$tmp_ko_list"
        set -e  # Re-enable exit-on-error
        rm -f "$tmp_ko_list"
        
        local summary=""
        [ $replaced_count -gt 0 ] && summary="${replaced_count} SAS/SCSI module(s) replaced"
        [ $added_count -gt 0 ] && {
          [ -n "$summary" ] && summary="$summary, "
          summary="${summary}${added_count} module(s) added"
        }
        
        if [ -n "$summary" ]; then
          log_info "  ✓ $summary"
        else
          log_info "  ✓ No modules replaced or added"
        fi
      else
        # For standard mode, track storage module replacements
        local tmp_ko_list=$(mktemp)
        find "${COMPILED_OUTPUT_DIR}" -type f -name "*.ko" > "$tmp_ko_list" 2>&1 || true
        
        local replaced_storage_count=0
        local total_copied=0
        set +e  # Disable exit-on-error for this loop
        while IFS= read -r ko_file; do
          if [ -z "$ko_file" ]; then continue; fi
          local basename=$(basename "$ko_file" .ko)
          
          # Check if it's a storage module and exists in thirdparty
          if is_storage_module "$basename" "$PLATFORM" "$KVER" >/dev/null 2>&1; then
            if [ -f "${MERGED_STAGING_DIR}/$(basename "$ko_file")" ]; then
              log_info "    ↻ Replacing thirdparty $(basename "$ko_file") with compiled version"
              ((replaced_storage_count++))
            fi
          fi
          
          cp "$ko_file" "${MERGED_STAGING_DIR}/" >/dev/null 2>&1 && ((total_copied++))
        done < "$tmp_ko_list"
        set -e  # Re-enable exit-on-error
        rm -f "$tmp_ko_list"
        
        if [ $replaced_storage_count -gt 0 ]; then
          log_info "  ✓ Merged $total_copied compiled module(s) ($replaced_storage_count SAS/SCSI storage modules replaced thirdparty versions)"
        else
          log_info "  ✓ Merged $total_copied compiled module(s)"
        fi
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
# Parameters: compile_mode (standard|movbe|full-merge)
select_and_compile() {
  local compile_mode=$1
  local mode_label=""
  
  case "$compile_mode" in
    movbe)
      mode_label="MOVBE Module"
      ;;
    full-merge)
      mode_label="Full Build + Thirdparty Merge + MOVBE for 7.3"
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
            full-merge)
              log_info "========================================"
              log_info "Building: ${PLATFORM} ${TOOLKIT_VER} (kernel ${KVER})"
              log_info "========================================"
              
              # Step 1: Compile all modules (skip auto-merge)
              compile_modules "$PLATFORM" "$KVER" "$TOOLKIT_VER" "$DOCKER_IMAGE" 1
              
              # Step 2: For 7.3 builds, compile MOVBE module (skip auto-merge)
              if [ "$TOOLKIT_VER" = "7.3" ]; then
                log_info "Additional: Compiling MOVBE module for 7.3 build..."
                
                # Compile MOVBE without merge
                DIR="${KVER:0:1}.x"
                DEFINES_FILE="${PWD}/${DIR}/defines.${PLATFORM}"
                
                if [ -d "${PWD}/movbe" ] && [ -f "${PWD}/movbe/Makefile" ]; then
                  TEMP_MOVBE_INPUT="${PWD}/.movbe-input-${PLATFORM}-${KVER}"
                  mkdir -p "${TEMP_MOVBE_INPUT}"
                  
                  cp "${PWD}/movbe"/*.c "${TEMP_MOVBE_INPUT}/" 2>/dev/null || true
                  cp "${PWD}/movbe"/Makefile "${TEMP_MOVBE_INPUT}/" 2>/dev/null || true
                  
                  {
                    head -n 1 "${DEFINES_FILE}"
                    cat "${PWD}/movbe/defines.movbe"
                  } > "${TEMP_MOVBE_INPUT}/defines.${PLATFORM}"
                  
                  OUTPUT_DIR="${PWD}/output/${PLATFORM}-${KVER}"
                  mkdir -p "${OUTPUT_DIR}"
                  LOG_FILE="${PWD}/logs/compile-${PLATFORM}-${TOOLKIT_VER}-${KVER}-movbe.txt"
                  
                  if docker run -u $(id -u) --rm -t -v "${TEMP_MOVBE_INPUT}":/input -v "${OUTPUT_DIR}":/output \
                    -e CFLAGS="-Wno-address -Wno-unused-result -Wno-misleading-indentation -Wno-array-parameter -Wno-unused-function" \
                    ${DOCKER_IMAGE} compile-module "${PLATFORM}" 2>&1 | tee -a "${LOG_FILE}"; then
                    log_info "✓ MOVBE module compiled successfully"
                  else
                    log_error "✗ MOVBE module compilation failed"
                  fi
                  
                  rm -rf "${TEMP_MOVBE_INPUT}"
                fi
              fi
              
              # Step 3: Merge with thirdparty, replacing storage modules from list + MOVBE
              merge_with_thirdparty "$PLATFORM" "$KVER" "$TOOLKIT_VER" "storage-only"
              
              log_info "✓ Completed build for ${PLATFORM} ${TOOLKIT_VER}"
              echo ""
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
  echo "3) Compile all modules + merge thirdparty + MOVBE for 7.3"
  echo ""
  read -r -p "Select compilation mode: " compile_mode
  echo ""
  
  case "$compile_mode" in
    2)
      select_and_compile "movbe"
      ;;
    3)
      select_and_compile "full-merge"
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