# Milkv Duo Kernel Module Development Setup
This repository provides kernel modules for embedded linux kernel (tested on milkv duo boards) and the necessary configuration to compile them.

## Build
1. Clone the repo:
```bash
git clone https://github.com/marcosraimondi1/milkv-duo-dev-modules.git my-modules
cd my-modules
```
2. Export the sdk directory variable, this is necessary to use the same toolchain:
```bash
export SDK_DIR?=/path/to/duo-buildroot-sdk 
```

If not using the duo sdk you can skip this step and the next one. 
Just make sure to export the following variables into your system:
```bash
export ARCH=arm
export CROSS_COMPILE=arm-buildroot-linux-gnueabi-
export LINUXDIR=/path/to/some/build/linux-x.x.x
```
3. Source config file:
```bash
source envsetup.sh
```
4. Build modules:
```bash
cd dummy
make
```
5. Copy .ko file to the board and install the module (ssh default password is milkv):
```bash
scp dummy.ko root@192.168.42.1:/root/
ssh root@192.168.42.1
insmod dummy.ko # install module
rmmod dummy.ko # remove module
dmesg # check kernel log to confirm it's working
```

## Note
Some modules require changes on the linux kernel sources for milkv duo, check this forked repository for 
a compatible version of it [marcosraimondi1/duo-buildroot-sdk](https://github.com/marcosraimondi1/duo-buildroot-sdk/).

Some modules are meant to interact with userspace applications, you can see some examples on this 
other repository [marcosraimondi1/milkv-duo-dev-apps](https://github.com/marcosraimondi1/milkv-duo-dev-apps).
