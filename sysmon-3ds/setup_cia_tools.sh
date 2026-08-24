#!/bin/bash
set -e

mkdir -p tools
cd tools

# Download makerom
if [ ! -f makerom ]; then
    echo "Downloading makerom..."
    if command -v curl &> /dev/null; then
        curl -fsSL "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.3/makerom-v0.18.3-ubuntu_x86_64.zip" -o "makerom.zip"
    else
        wget -q "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.3/makerom-v0.18.3-ubuntu_x86_64.zip" -O "makerom.zip"
    fi
    if command -v bsdtar &> /dev/null; then
        bsdtar -xf makerom.zip
    elif command -v unzip &> /dev/null; then
        unzip -o -q makerom.zip
    else
        python3 -m zipfile -e makerom.zip .
    fi
    mv makerom-v0.18.3-ubuntu_x86_64 makerom
    chmod +x makerom
    rm -rf makerom.zip
fi

# Build bannertool from source for guaranteed glibc compatibility
echo "Building bannertool from source..."
rm -rf /tmp/bannertool_build
git clone --depth 1 https://github.com/carstene1ns/3ds-bannertool.git /tmp/bannertool_build
cmake -B /tmp/bannertool_build/build -S /tmp/bannertool_build
cmake --build /tmp/bannertool_build/build -j"$(nproc)"
cp /tmp/bannertool_build/build/bannertool ./bannertool
chmod +x ./bannertool
rm -rf /tmp/bannertool_build

echo "3DS tools setup complete in sysmon-3ds/tools/"
