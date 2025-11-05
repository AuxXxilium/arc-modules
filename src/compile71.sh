#!/usr/bin/env bash

set -e

TOOLKIT_VER="7.1"

echo -e "Compiling modules..."
while read PLATFORM KVER; do
  [ -n "$1" -a "${PLATFORM}" != "$1" ] && continue
  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && continue
  mkdir -p "/tmp/${PLATFORM}-${KVER}"

  # Ensure the defines.x file is respected
  DEFINES_FILE="defines.${PLATFORM}"
  if [ -f "${PWD}/${DIR}/${DEFINES_FILE}" ]; then
    cp "${PWD}/${DIR}/${DEFINES_FILE}" "${PWD}/${DIR}/.config"
  else
    echo "Warning: ${DEFINES_FILE} not found for ${PLATFORM}"
  fi

  # Prepare the Docker run parameters
  runparam=$(echo "-u `id -u` --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
    fbelavenuto/syno-compiler:${TOOLKIT_VER} compile-module ${PLATFORM}")
  echo $runparam

  # Run the Docker container
  docker run -u `id -u` --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
    fbelavenuto/syno-compiler:${TOOLKIT_VER} compile-module ${PLATFORM}

  # Handle output directory naming
  if [ "${PLATFORM}" = "epyc7002" ]; then
    PLATFORM_DIR="${PLATFORM}-${TOOLKIT_VER}-${KVER}"
  else
    PLATFORM_DIR="${PLATFORM}-${KVER}"
  fi
  rm -rf ${PWD}/../${PLATFORM_DIR}

  # Copy compiled modules and clean up
  for M in `ls /tmp/${PLATFORM}-${KVER}`; do
    [ -f ~/src/pats/modules/${PLATFORM}/$M ] && \
    cp ~/src/pats/modules/${PLATFORM}/$M "${PWD}/../${PLATFORM_DIR}/" || \
    { mkdir -p "${PWD}/../${PLATFORM_DIR}" && cp /tmp/${PLATFORM}-${KVER}/$M "${PWD}/../${PLATFORM_DIR}/"; }
    # Remove unwanted modules
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbfillrect.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbfillrect.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbimgblt.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbimgblt.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbcopyarea.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/cfbcopyarea.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/video.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/video.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/backlight.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/backlight.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/button.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/button.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/drm_kms_helper.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/drm_kms_helper.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/drm.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/drm.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/fb.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/fb.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/fbdev.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/fbdev.ko 
    [[ -f ${PWD}/../${PLATFORM}-${KVER}/i2c-algo-bit.ko ]] && rm ${PWD}/../${PLATFORM_DIR}/i2c-algo-bit.ko
  done
  rm -rf /tmp/${PLATFORM}-${KVER}
done < PLATFORMS71