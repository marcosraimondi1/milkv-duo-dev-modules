#!/bin/bash

BOARD=milkv-duo256m-sd
BOARD_CHIP=cv1812cp_milkv_duo256m_sd

# BOARD=milkv-duo-sd
# BOARD_CHIP=cv1800b_milkv_duo_sd

CURRENT_DIR=$(pwd)
SDK_DIR=$CURRENT_DIR/../duo-buildroot-sdk
cd $SDK_DIR
source $SDK_DIR/device/$BOARD/boardconfig.sh > /dev/null
source $SDK_DIR/build/milkvsetup.sh > /dev/null
defconfig $BOARD_CHIP > /dev/null
cd $CURRENT_DIR
# export ARCH=riscv
# export CROSS_COMPILE=riscv64-unknown-linux-musl-
export LINUXDIR=$SDK_DIR/linux_5.10/build/$BOARD_CHIP
