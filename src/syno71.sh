#!/usr/bin/env bash

set -e

#git pull

TOOLKIT_VER="${1}"
PLATFORM="${2}"
KVER="${3}"

echo -e "Compiling modules ${1} ${2} ${3}"

DIR="${KVER:0:1}.x"

[ ! -d "${PWD}/${DIR}" ] && exit 1

mkdir -p "/tmp/${PLATFORM}-${KVER}"

docker run -u `id -u` --rm -t -v "${PWD}/${DIR}":/input -v "/tmp/${PLATFORM}-${KVER}":/output \
  dante90/syno-compiler:${TOOLKIT_VER} compile-module ${PLATFORM}

# 출력 디렉터리 생성
PLATFORM_DIR="${PLATFORM}-${TOOLKIT_VER}-${KVER}"
mkdir -p "${PWD}/../output/${PLATFORM_DIR}"

for M in `ls /tmp/${PLATFORM}-${KVER}`; do
  # 컴파일된 모듈 복사
  cp /tmp/${PLATFORM}-${KVER}/$M "${PWD}/../output/${PLATFORM_DIR}/"
  
  # i915 관련 모듈 제거
  case "$M" in
    cfbfillrect.ko|cfbimgblt.ko|cfbcopyarea.ko|video.ko|backlight.ko|button.ko|drm_kms_helper.ko|drm.ko|fb.ko|fbdev.ko|i2c-algo-bit.ko)
      rm -f "${PWD}/../output/${PLATFORM_DIR}/$M"
      ;;
  esac
done

rm -rf /tmp/${PLATFORM}-${KVER}
