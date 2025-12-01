#!/usr/bin/env bash

set -e

# Define constants
ROOT_PATH=$(pwd)
PKGSCRIPTS_REPO="https://github.com/SynologyOpenSource/pkgscripts-ng.git"
PKGSCRIPTS_DIR="${ROOT_PATH}/pkgscripts-ng"
BUILD_ENV_DIR="${ROOT_PATH}/build_env"
SOURCE_DIR="${ROOT_PATH}/source"
PLATFORMS_FILE="PLATFORMS"

# Step 1: Clone pkgscripts-ng repository
echo "Cloning pkgscripts-ng repository..."
if [ ! -d "${PKGSCRIPTS_DIR}" ]; then
  git clone "${PKGSCRIPTS_REPO}" "${PKGSCRIPTS_DIR}"
fi

# Step 2: Deploy the environment
echo "Deploying the environment..."
cd "${PKGSCRIPTS_DIR}"
git checkout DSM7.2  # Adjust the version as needed
sudo ./EnvDeploy -v 7.2 -l  # List available platforms
sudo ./EnvDeploy -q -v 7.2 -p epyc7002  # Deploy for the specific platform

# Step 3: Prepare the environment variables
ENV_PATH="${BUILD_ENV_DIR}/ds.epyc7002-7.2"
if [ ! -d "${ENV_PATH}" ]; then
  mkdir -p "${ENV_PATH}"
  sudo cp -al "${PKGSCRIPTS_DIR}" "${ENV_PATH}/"
fi

# Step 4: Extract kernel version and compiler version
echo "Extracting kernel and compiler versions..."
sudo chroot "${ENV_PATH}" << "EOF"
cd pkgscripts
version=7.2
sed -i 's/print(" ".join(kernels))/pass #&/' ProjectDepends.py
sed -i '/PLATFORM_FAMILY/a\\techo "PRODUCT=$PRODUCT" >> $file\n\techo "KSRC=$KERNEL_SEARCH_PATH" >> $file\n\techo "LINUX_SRC=$KERNEL_SEARCH_PATH" >> $file' include/build
./SynoBuild -c -p epyc7002

while read line; do
  if [ ${line:0:1} != "#" ]; then
    export ${line%%=*}="${line#*=}"
  fi
done < /env64.mak

if [ -f "${KSRC}/Makefile" ]; then
  VERSION=$(grep ^VERSION "${KSRC}/Makefile" | awk '{print $3}')
  PATCHLEVEL=$(grep ^PATCHLEVEL "${KSRC}/Makefile" | awk '{print $3}')
  SUBLEVEL=$(grep ^SUBLEVEL "${KSRC}/Makefile" | awk '{print $3}')
  echo "KVER=${VERSION}.${PATCHLEVEL}.${SUBLEVEL}" >> /env64.mak
  CCVER=$($CC --version | head -n 1 | awk '{print $3}')
  echo "CCVER=${CCVER}" >> /env64.mak
fi
EOF

# Step 5: Prepare the source directory
echo "Preparing the source directory..."
mkdir -p "${SOURCE_DIR}/output"
sudo cp -a "${ROOT_PATH}/src/5.x" "${SOURCE_DIR}/input"
sudo cp -a "${SOURCE_DIR}" "${ENV_PATH}/"

# Step 6: Call compile.sh
echo "Executing compile.sh..."
bash "${ROOT_PATH}/compile.sh" epyc7002