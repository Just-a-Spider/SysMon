#!/bin/bash
mkdir -p tools
cd tools

wget https://github.com/Steveice10/bannertool/releases/download/1.2.0/bannertool.zip
unzip bannertool.zip
mv linux-x86_64/bannertool .
chmod +x bannertool

wget https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.3/makerom-v0.18.3-ubuntu_x86_64.zip
unzip makerom-v0.18.3-ubuntu_x86_64.zip
mv makerom-v0.18.3-ubuntu_x86_64 makerom
chmod +x makerom

rm -rf linux* windows* mac* *.zip makerom-v0*
echo "Tools installed to sys_mon/tools/"
