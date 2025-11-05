#!/usr/bin/env bash

set -e

git pull

#if [ -f ../../arpl/PLATFORMS ]; then
#  cp ../../arpl/PLATFORMS PLATFORMS
#else
#  curl -sLO "https://github.com/fbelavenuto/arpl/raw/main/PLATFORMS"
#fi

echo -e "Compiling modules..."
while read PLATFORM KVER TOOLKIT_VER; do
  [ -n "$1" -a "${PLATFORM}" != "$1" ] && continue
  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && continue
  mkdir -p "/tmp/${PLATFORM}-${KVER}"
  docker run -u `id -u` --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
    dante90/syno-compiler:${TOOLKIT_VER} compile-module ${PLATFORM}
  for M in `ls /tmp/${PLATFORM}-${KVER}`; do
    PLATFORM_DIR="${PLATFORM}-${TOOLKIT_VER}-${KVER}"
    rm -rf ${PWD}/../${PLATFORM_DIR}
    [ -f ~/src/pats/modules/${PLATFORM}/$M ] && \
    cp ~/src/pats/modules/${PLATFORM}/$M "${PWD}/../${PLATFORM_DIR}/" || \
    { mkdir -p "${PWD}/../${PLATFORM_DIR}" && cp /tmp/${PLATFORM}-${KVER}/$M "${PWD}/../${PLATFORM_DIR}/"; }
    # remove i915 related modules
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
  
done < PLATFORMS5X
