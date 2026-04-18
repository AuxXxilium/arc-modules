#!/usr/bin/env bash

set -e

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

  # Handle allow-store-data-races flag for kernel versions 7.2 and 7.3
  if [ "$TOOLKIT_VER" = "7.2" ] || [ "$TOOLKIT_VER" = "7.3" ]; then
    sed -i 's/--param=allow-store-data-races=0/--allow-store-data-races/g' "${PWD}/${DIR}/Makefile"
  fi

  # Run the Docker container
  log_info "Starting Docker compilation for ${PLATFORM} (kernel ${KVER})"
  if docker run -u $(id -u) --rm -t -v "${PWD}/${DIR}":/input -v "${OUTPUT_DIR}":/output \
    ${DOCKER_IMAGE} compile-module "${PLATFORM}"; then
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

  # Run the Docker container using the proper compile-module function from do.sh
  log_info "Starting Docker MOVBE module compilation for ${PLATFORM} (kernel ${KVER})"
  if docker run -u $(id -u) --rm -t -v "${TEMP_MOVBE_INPUT}":/input -v "${OUTPUT_DIR}":/output \
    ${DOCKER_IMAGE} compile-module "${PLATFORM}"; then
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

  # Run the Docker container
  log_info "Starting binary compilation for ${PLATFORM} using ${BUILD_SCRIPT}"
  if docker run --privileged -u $(id -u) --rm -t -v "${PWD}/${DIR}":/input -v "${OUTPUT_DIR}":/output \
    ${DOCKER_IMAGE} compile-binary "${PLATFORM}" "${BUILD_SCRIPT}"; then
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

  # Extract unique toolkit versions
  local -a versions=()
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
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
  
  # For each selected version, show platform selection
  for SELECTED_VERSION in "${selected_versions[@]}"; do
    log_info "=== Platforms for version ${SELECTED_VERSION} ==="
    echo ""
    
    # Parse platforms for this version
    local -a platforms=()
    local -a platform_data=()
    local idx=1
    
    while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
      [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
      TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
      [ "$TOOLKIT_VER" != "$SELECTED_VERSION" ] && continue
      
      PLATFORM=$(echo "${PLATFORM}" | xargs)
      KVER=$(echo "${KVER}" | xargs)
      DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
      
      platforms+=("$PLATFORM")
      platform_data+=("$KVER|$DOCKER_IMAGE")
      printf "%2d) %-20s (kernel %s)\n" "$idx" "$PLATFORM" "$KVER"
      idx=$((idx + 1))
    done < "${PLATFORMS_FILE}"
    
    echo ""
    echo "Enter platform numbers to build (space-separated, or 'all' for all platforms):"
    read -r -p "> " platform_selection
    
    echo ""
    
    if [ -z "$platform_selection" ] || [ "$platform_selection" = "all" ]; then
      for i in "${!platforms[@]}"; do
        local data="${platform_data[$i]}"
        local KVER="${data%%|*}"
        local DOCKER_IMAGE="${data#*|}"
        compile_modules "${platforms[$i]}" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
      done
    else
      for num in $platform_selection; do
        if [[ "$num" =~ ^[0-9]+$ ]] && [ "$num" -ge 1 ] && [ "$num" -le "${#platforms[@]}" ]; then
          local idx=$((num - 1))
          local data="${platform_data[$idx]}"
          local KVER="${data%%|*}"
          local DOCKER_IMAGE="${data#*|}"
          compile_modules "${platforms[$idx]}" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
        else
          log_warn "Invalid platform number $num"
        fi
      done
    fi
  done
}

# Function to select platforms and versions for MOVBE module compilation
select_platforms_movbe() {
  local PLATFORMS_FILE="PLATFORMS"
  [ ! -f "${PLATFORMS_FILE}" ] && { log_error "${PLATFORMS_FILE} not found."; exit 1; }

  # Extract unique toolkit versions
  local -a versions=()
  while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
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
  
  # For each selected version, show platform selection
  for SELECTED_VERSION in "${selected_versions[@]}"; do
    log_info "=== Platforms for version ${SELECTED_VERSION} (MOVBE Module) ==="
    echo ""
    
    # Parse platforms for this version
    local -a platforms=()
    local -a platform_data=()
    local idx=1
    
    while read -r PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
      [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
      TOOLKIT_VER=$(echo "${TOOLKIT_VER}" | xargs)
      [ "$TOOLKIT_VER" != "$SELECTED_VERSION" ] && continue
      
      PLATFORM=$(echo "${PLATFORM}" | xargs)
      KVER=$(echo "${KVER}" | xargs)
      DOCKER_IMAGE=$(echo "${DOCKER_IMAGE}" | xargs)
      
      platforms+=("$PLATFORM")
      platform_data+=("$KVER|$DOCKER_IMAGE")
      printf "%2d) %-20s (kernel %s)\n" "$idx" "$PLATFORM" "$KVER"
      idx=$((idx + 1))
    done < "${PLATFORMS_FILE}"
    
    echo ""
    echo "Enter platform numbers to build (space-separated, or 'all' for all platforms):"
    read -r -p "> " platform_selection
    
    echo ""
    
    if [ -z "$platform_selection" ] || [ "$platform_selection" = "all" ]; then
      for i in "${!platforms[@]}"; do
        local data="${platform_data[$i]}"
        local KVER="${data%%|*}"
        local DOCKER_IMAGE="${data#*|}"
        compile_movbe_module "${platforms[$i]}" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
      done
    else
      for num in $platform_selection; do
        if [[ "$num" =~ ^[0-9]+$ ]] && [ "$num" -ge 1 ] && [ "$num" -le "${#platforms[@]}" ]; then
          local idx=$((num - 1))
          local data="${platform_data[$idx]}"
          local KVER="${data%%|*}"
          local DOCKER_IMAGE="${data#*|}"
          compile_movbe_module "${platforms[$idx]}" "$KVER" "$SELECTED_VERSION" "$DOCKER_IMAGE"
        else
          log_warn "Invalid platform number $num"
        fi
      done
    fi
  done
}


main() {
  log_info "=== Module Compiler (Docker) ==="
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