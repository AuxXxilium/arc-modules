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
    ${DOCKER_IMAGE} compile-module "${PLATFORM}" 2>&1 | tee -a "${LOG_FILE}"; then
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

  # Handle output directory naming and packaging
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

  # Merge with thirdparty modules and create final package
  merge_with_thirdparty "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}"
}

# Function to merge compiled modules with thirdparty base and create final package
merge_with_thirdparty() {
  local PLATFORM=$1
  local KVER=$2
  local TOOLKIT_VER=$3
  local MODULE_TYPE=${4:-""}  # Optional: "movbe" or empty

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

  # Step 1: Copy all thirdparty modules to staging area
  if cp -r "${THIRDPARTY_PLATFORM_DIR}"/* "${MERGED_STAGING_DIR}/" 2>/dev/null; then
    log_info "  ✓ Copied thirdparty base modules"
  else
    log_warn "  ⚠ Could not copy thirdparty modules (directory may be empty)"
  fi

  # Step 2: Copy compiled .ko files, overwriting thirdparty versions
  local COMPILED_OUTPUT_DIR="${PWD}/output/${PLATFORM}-${KVER}${SUFFIX}"
  if [ -d "${COMPILED_OUTPUT_DIR}" ]; then
    local COMPILED_COUNT=$(find "${COMPILED_OUTPUT_DIR}" -maxdepth 1 -type f -name "*.ko" 2>/dev/null | wc -l)
    if [ "$COMPILED_COUNT" -gt 0 ]; then
      cp "${COMPILED_OUTPUT_DIR}"/*.ko "${MERGED_STAGING_DIR}/" 2>/dev/null || true
      log_info "  ✓ Merged $COMPILED_COUNT compiled module(s) (replacing thirdparty versions)"
    fi
  fi

  # Step 3: Create final tarball in merged-output
  local MERGED_PACKAGE_NAME="${PLATFORM}-${TOOLKIT_VER}-${KVER}${SUFFIX}.tgz"
  local MERGED_TARBALL_PATH="${MERGED_OUTPUT_ROOT}/${MERGED_PACKAGE_NAME}"

  log_info "Creating merged package: ${MERGED_TARBALL_PATH}"
  if tar --exclude="*.tgz" -czf "${MERGED_TARBALL_PATH}" -C "${MERGED_STAGING_DIR}" . 2>/dev/null; then
    if [ -f "${MERGED_TARBALL_PATH}" ]; then
      local SIZE=$(du -h "${MERGED_TARBALL_PATH}" | awk '{print $1}')
      log_info "✓ Successfully created merged package: ${MERGED_TARBALL_PATH} (${SIZE})"
    else
      log_error "✗ Merged tarball not found after creation"
      rm -rf "${MERGED_STAGING_DIR}"
      return 1
    fi
  else
    log_error "✗ Failed to create merged tarball"
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

# Function to display platform and version selection menu
select_platforms() {
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
  
  # Sort versions
  IFS=$'\n' versions=($(sort <<<"${versions[*]}"))
  unset IFS

  # Display version selection menu
  log_info "=== Available DSM/Toolkit Versions ==="
  echo ""
  for i in "${!versions[@]}"; do
    printf "%2d) %s\n" $((i+1)) "${versions[$i]}"
  done
  
  echo ""
  echo "Enter version numbers to build (space-separated, or 'all' for all versions):"
  read -r -p "> " version_selection
  
  echo ""
  
  # Determine selected versions
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
  
  # Build set of all platform combinations for selected versions
  log_info "=== Available Platforms ==="
  echo ""
  
  local -a all_platforms=()
  local -a all_platform_data=()
  local idx=1
  
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
    
    # Skip if version not in selected versions
    local skip=1
    for sel_ver in "${selected_versions[@]}"; do
      if [ "$TOOLKIT_VER" = "$sel_ver" ]; then
        skip=0
        break
      fi
    done
    [ $skip -eq 1 ] && continue
    
    PLATFORM=$(echo "${PLATFORM}" | xargs)
    KVER=$(echo "${KVER}" | xargs)
    DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
    
    # Check if platform already listed
    local platform_exists=0
    for existing in "${all_platforms[@]}"; do
      if [ "$existing" = "$PLATFORM" ]; then
        platform_exists=1
        break
      fi
    done
    
    if [ $platform_exists -eq 0 ]; then
      all_platforms+=("$PLATFORM")
      printf "%2d) %s\n" "$idx" "$PLATFORM"
      idx=$((idx + 1))
    fi
  done < "${PLATFORMS_FILE}"
  
  echo ""
  echo "Enter platform numbers to build (space-separated, or 'all' for all platforms):"
  read -r -p "> " platform_selection
  
  echo ""
  
  # Determine selected platforms
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
  
  # Now compile all combinations of selected platforms and versions
  for SELECTED_PLATFORM in "${selected_platforms[@]}"; do
    for SELECTED_VERSION in "${selected_versions[@]}"; do
      while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
        [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
        PLATFORM=$(echo "${PLATFORM}" | xargs)
        TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
        KVER=$(echo "${KVER}" | xargs)
        DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
        
        if [ "$PLATFORM" = "$SELECTED_PLATFORM" ] && [ "$TOOLKIT_VER" = "$SELECTED_VERSION" ]; then
          compile_modules "$SELECTED_PLATFORM" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
          break
        fi
      done < "${PLATFORMS_FILE}"
    done
  done
}

# Function to select platforms and versions for MOVBE module compilation
select_platforms_movbe() {
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
  
  # Sort versions
  IFS=$'\n' versions=($(sort <<<"${versions[*]}"))
  unset IFS

  # Display version selection menu
  log_info "=== Available DSM/Toolkit Versions (MOVBE Module) ==="
  echo ""
  for i in "${!versions[@]}"; do
    printf "%2d) %s\n" $((i+1)) "${versions[$i]}"
  done
  
  echo ""
  echo "Enter version numbers to build (space-separated, or 'all' for all versions):"
  read -r -p "> " version_selection
  
  echo ""
  
  # Determine selected versions
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
  
  # Build set of all platform combinations for selected versions
  log_info "=== Available Platforms (MOVBE Module) ==="
  echo ""
  
  local -a all_platforms=()
  local idx=1
  
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
    
    # Skip if version not in selected versions
    local skip=1
    for sel_ver in "${selected_versions[@]}"; do
      if [ "$TOOLKIT_VER" = "$sel_ver" ]; then
        skip=0
        break
      fi
    done
    [ $skip -eq 1 ] && continue
    
    PLATFORM=$(echo "${PLATFORM}" | xargs)
    
    # Check if platform already listed
    local platform_exists=0
    for existing in "${all_platforms[@]}"; do
      if [ "$existing" = "$PLATFORM" ]; then
        platform_exists=1
        break
      fi
    done
    
    if [ $platform_exists -eq 0 ]; then
      all_platforms+=("$PLATFORM")
      printf "%2d) %s\n" "$idx" "$PLATFORM"
      idx=$((idx + 1))
    fi
  done < "${PLATFORMS_FILE}"
  
  echo ""
  echo "Enter platform numbers to build (space-separated, or 'all' for all platforms):"
  read -r -p "> " platform_selection
  
  echo ""
  
  # Determine selected platforms
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
  
  # Now compile all combinations of selected platforms and versions
  for SELECTED_PLATFORM in "${selected_platforms[@]}"; do
    for SELECTED_VERSION in "${selected_versions[@]}"; do
      while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
        [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
        PLATFORM=$(echo "${PLATFORM}" | xargs)
        TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
        KVER=$(echo "${KVER}" | xargs)
        DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
        
        if [ "$PLATFORM" = "$SELECTED_PLATFORM" ] && [ "$TOOLKIT_VER" = "$SELECTED_VERSION" ]; then
          compile_movbe_module "$SELECTED_PLATFORM" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
          break
        fi
      done < "${PLATFORMS_FILE}"
    done
  done
}


main() {
  log_info "=== Module Compiler (Docker) ==="
  echo ""

  # Clean and create logs directory
  if [ -d "${PWD}/logs" ]; then
    log_info "Cleaning old logs..."
    rm -rf "${PWD}/logs"
  fi
  mkdir -p "${PWD}/logs"
  log_info "Build logs will be saved to: ${PWD}/logs"
  echo ""

  # Check if the unified PLATFORMS file exists
  PLATFORMS_FILE="PLATFORMS"
  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "${PLATFORMS_FILE} not found."; exit 1; }

  # Show compilation type menu
  log_info "=== Compilation Mode ==="
  echo ""
  echo "1) Compile standard modules"
  echo "2) Compile MOVBE module only"
  echo ""
  echo "Select compilation mode:"
  read -r -p "> " compile_mode
  
  case "$compile_mode" in
    2)
      # MOVBE module compilation
      select_platforms_movbe
      ;;
    1|*)
      # Standard modules compilation (default)
      if [ -n "$1" ]; then
        log_info "Compiling modules for platform: $1"

        while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
          # Skip comments and empty lines
          [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
          PLATFORM=$(echo "${PLATFORM}" | xargs)

          # Case-insensitive comparison
          [ "$(echo "${PLATFORM}" | tr '[:upper:]' '[:lower:]')" != "$(echo "$1" | tr '[:upper:]' '[:lower:]')" ] && continue

          KVER=$(echo "${KVER}" | xargs)
          TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
          DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)

          compile_modules "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}" "${DOCKER_IMAGE}"
        done < "${PLATFORMS_FILE}"
      else
        # Interactive platform selection
        select_platforms
      fi
      ;;
  esac
}

# Run the main function
main "$@"