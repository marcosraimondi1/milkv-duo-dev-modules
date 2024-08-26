#!/bin/bash

CURRENT_DIR=$(pwd)
SDK_DIR=/home/marcos/facu/tesis/SDKs/duo-buildroot-sdk
cd $SDK_DIR
source $SDK_DIR/device/milkv-duo256m-sd/boardconfig.sh > /dev/null
source $SDK_DIR/build/milkvsetup.sh > /dev/null
defconfig cv1812cp_milkv_duo256m_sd > /dev/null
cd $CURRENT_DIR
# export ARCH=riscv
# export CROSS_COMPILE=riscv64-unknown-linux-musl-
export LINUXDIR=$SDK_DIR/linux_5.10/build/cv1812cp_milkv_duo256m_sd
