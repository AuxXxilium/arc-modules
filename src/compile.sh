#!/usr/bin/env bash

set -e

# Function to compile modules for a given platform and kernel version
compile_modules() {
  local PLATFORM=$1
  local KVER=$2
  local TOOLKIT_VER=$3
  local DOCKER_IMAGE=$4

  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && return

  mkdir -p "/tmp/${PLATFORM}-${KVER}"

  # Check if the defines.<platform> file exists
  DEFINES_FILE="${PWD}/${DIR}/defines.${PLATFORM}"
  if [ ! -f "${DEFINES_FILE}" ]; then
    echo "Error: ${DEFINES_FILE} not found for platform ${PLATFORM}."
    exit 1
  fi

  # Prepare the Docker run parameters
  runparam=$(echo "-u $(id -u) --rm -t -v \"${PWD}/${DIR}\":/input -v \"/tmp/${PLATFORM}-${KVER}\":/output \
    ${DOCKER_IMAGE} compile-module ${PLATFORM} --kconfig ${DEFINES_FILE}")
  echo $runparam

  # Run the Docker container
  docker run -u $(id -u) --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
    ${DOCKER_IMAGE} compile-module ${PLATFORM} --kconfig ${DEFINES_FILE}

  # Handle output directory naming
  if [ "${PLATFORM}" = "epyc7002" ] || [ "${PLATFORM}" = "geminilakenk" ] || [ "${PLATFORM}" = "r1000nk" ] || [ "${PLATFORM}" = "v1000nk" ]; then
    PLATFORM_DIR="${PLATFORM}-${TOOLKIT_VER}-${KVER}"
  else
    PLATFORM_DIR="${PLATFORM}-${KVER}"
  fi
  rm -rf "${PWD}/../${PLATFORM_DIR}"

  # Copy compiled modules and clean up
  for M in $(ls /tmp/${PLATFORM}-${KVER}); do
    [ -f ~/src/pats/modules/${PLATFORM}/$M ] && \
    cp ~/src/pats/modules/${PLATFORM}/$M "${PWD}/../${PLATFORM_DIR}/" || \
    { mkdir -p "${PWD}/../${PLATFORM_DIR}" && cp /tmp/${PLATFORM}-${KVER}/$M "${PWD}/../${PLATFORM_DIR}/"; }
    # Remove unwanted modules
    [[ -f ${PWD}/../${PLATFORM_DIR}/cfbfillrect.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbfillrect.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/cfbimgblt.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbimgblt.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/cfbcopyarea.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbcopyarea.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/video.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/video.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/backlight.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/backlight.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/button.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/button.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/drm_kms_helper.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/drm_kms_helper.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/drm.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/drm.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/fb.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/fb.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/fbdev.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/fbdev.ko 
    [[ -f ${PWD}/../${PLATFORM_DIR}/i2c-algo-bit.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/i2c-algo-bit.ko
  done
  rm -rf /tmp/${PLATFORM}-${KVER}
}

# Main function to handle different platforms and toolkits
main() {
  echo -e "Compiling modules..."

  # Check if the unified PLATFORMS file exists
  PLATFORMS_FILE="PLATFORMS_ALL"
  [ ! -f "${PLATFORMS_FILE}" ] && { echo "Error: ${PLATFORMS_FILE} not found."; exit 1; }

  # Read the unified PLATFORMS file
  while read PLATFORM KVER TOOLKIT_VER DOCKER_IMAGE; do
    # Skip comments and empty lines
    [[ "$PLATFORM" =~ ^#.*$ || -z "$PLATFORM" ]] && continue
    [ -n "$1" -a "${PLATFORM}" != "$1" ] && continue
    compile_modules "${PLATFORM}" "${KVER}" "${TOOLKIT_VER}" "${DOCKER_IMAGE}"
  done < "${PLATFORMS_FILE}"
}

# Run the main function
main "$@"