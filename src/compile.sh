#!/usr/bin/env bash

set -e

TOOLKIT_VER="7.2"

#if [ -f ../../arpl/PLATFORMS ]; then
#  cp ../../arpl/PLATFORMS PLATFORMS
#else
#  curl -sLO "https://github.com/fbelavenuto/arpl/raw/main/PLATFORMS"
#fi

echo -e "Compiling modules..."
while read PLATFORM KVER; do
  [ -n "$1" -a "${PLATFORM}" != "$1" ] && continue
  DIR="${KVER:0:1}.x"
  [ ! -d "${PWD}/${DIR}" ] && continue
  mkdir -p "/tmp/${PLATFORM}-${KVER}"
  #docker run --rm -t -v "${PWD}/${1}/${DIR}":/input -v "${PWD}/../${PLATFORM}-${KVER}":/output \
  #  fbelavenuto/syno-toolkit:${PLATFORM}-${TOOLKIT_VER} compile-module
  docker run -u `id -u` --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
    fbelavenuto/syno-compiler:${TOOLKIT_VER} compile-module ${PLATFORM}
  for M in `ls /tmp/${PLATFORM}-${KVER}`; do
    [ -f ~/src/pats/modules/${PLATFORM}/$M ] && \
      # original
      cp ~/src/pats/modules/${PLATFORM}/$M "${PWD}/../${PLATFORM}-${KVER}" || \
      # compiled
      cp /tmp/${PLATFORM}-${KVER}/$M "${PWD}/../${PLATFORM}-${KVER}"
      # remove i915 related modules
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbfillrect.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/cfbfillrect.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbimgblt.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/cfbimgblt.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/cfbcopyarea.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/cfbcopyarea.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/video.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/video.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/backlight.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/backlight.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/button.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/button.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/drm_kms_helper.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/drm_kms_helper.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/drm.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/drm.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/fb.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/fb.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/fbdev.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/fbdev.ko 
      [[ -f ${PWD}/../${PLATFORM}-${KVER}/i2c-algo-bit.ko ]] && rm ${PWD}/../${PLATFORM}-${KVER}/i2c-algo-bit.ko
  done
  rm -rf /tmp/${PLATFORM}-${KVER}
done < PLATFORMS

