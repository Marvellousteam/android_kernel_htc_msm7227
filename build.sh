#!/bin/bash

mkdir ../modules
mkdir ../out

# Colorize Scripts
red=$(tput setaf 1) # red
grn=$(tput setaf 2) # green
cya=$(tput setaf 6) # cyan
pnk=$(tput bold ; tput setaf 5) # pink
yel=$(tput bold ; tput setaf 3) # yellow
pur=$(tput setaf 5) # purple
txtbld=$(tput bold) # Bold
bldred=${txtbld}$(tput setaf 1) # red
bldgrn=${txtbld}$(tput setaf 2) # green
bldyel=${txtbld}$(tput bold ; tput setaf 3) # yellow
bldblu=${txtbld}$(tput setaf 4) # blue
bldpur=${txtbld}$(tput setaf 5) # purple
bldpnk=${txtbld}$(tput bold ; tput setaf 5) # pink
bldcya=${txtbld}$(tput setaf 6) # cyan
txtrst=$(tput sgr0) # Reset

ZIPFILENAME=./AureliusKernel-$TIMESTAMP-htc-msm7227-BETA0.zip
TOOLCHAIN="/home/olivier/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi"
KERNEL_DIR="/home/olivier/marvel/kernel"
MODULES_DIR="/home/olivier/marvel/modules"
ZIMAGE="/home/olivier/marvel/kernel/arch/arm/boot/zImage"
if [ -a $KERNEL_DIR/arch/arm/boot/zImage ];
then
rm $ZIMAGE
rm $MODULES_DIR/*
fi
res1=$(date +%s.%N)
echo "${bldyel}Do you want to build a JellyBean or a KitKat kernel? (jb/kk)${txtrst}"
read answer
if [ "$answer" = "jb" -o "$answer" = "JB" ]; then
echo "${bldblu}Starting the build (JellyBean kernel)${txtrst}"
script -q ~/kernel.log -c "make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- cyanogen_msm7227_jb-defconfig;make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- -j8"
else
echo "${bldpnk}Starting the build (KitKat kernel)${txtrst}"
script -q ~/kernel.log -c "make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- cyanogen_msm7227_kk-defconfig;make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- -j8"
fi
if [ -a $KERNEL_DIR/arch/arm/boot/zImage ];
then
echo "Copying modules"
rm $MODULES_DIR/*
find . -name '*.ko' -exec cp {} $MODULES_DIR/ \;
cd $MODULES_DIR
echo "${bldcya}Stripping modules for size${txtrst}"
$TOOLCHAIN-strip --strip-unneeded *.ko
cd $KERNEL_DIR
res2=$(date +%s.%N)
echo "${bldcya}Compilation successful! Total time elapsed: ${txtrst}${cya}$(echo "($res2 - $res1) / 60"|bc ) minutes ($(echo "$res2 - $res1"|bc ) seconds) ${txtrst}"
else
res2=$(date +%s.%N)
echo "${bldred}Compilation failed! Fix the errors! ${txtrst} ${bldcya}Total time elapsed: ${txtrst}${cya}$(echo "($res2 - $res1) / 60"|bc ) minutes ($(echo "$res2 - $res1"|bc ) seconds) ${txtrst}"
fi