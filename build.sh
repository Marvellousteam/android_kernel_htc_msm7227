#!/bin/bash

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

# Some defines
TOOLCHAIN="/home/olivier/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi"
KERNEL_DIR="/home/olivier/marvel/kernel"
MODULES_DIR="/home/olivier/marvel/modules"
ZIMAGE="/home/olivier/marvel/kernel/arch/arm/boot/zImage"

# Ask some questions
echo "${bldcya}What's your name?${txtrst}"
read NAME
echo "${bldcya}Alright, nice to meet you,${txtrst} ${bldgrn}$NAME!${txtrst} ${bldcya}Let me ask you a few questions before we build the kernel.${txtrst}"

echo "${bldcya}Do you want fetch the latest changes? ${txtrst} [y/n]"
read syncup

if [ "syncup" == "y" ]; then
echo "${bldcya}Syncing latest changes ${txtrst}"
git pull
fi

echo "${bldcya}Do you want to clean up? ${txtrst} [y/n]"
read cleanup

echo "${bldcya}How many cores do you want to use for the compile (enter a number)? ${txtrst}"
read cores

echo "${bldcya}What device do you want to build for? [chacha/cyanogen_msm7227/icong] ${txtrst}"
echo "${bldcya}NOTE: HTC ChaCha = chacha; HTC Aria/Wildfire S = cyanogen_msm7227; HTC Salsa = icong! ${txtrst}"
read PRODUCT

# Make clean
if [ "$cleanup" == "y" ]; then
echo "${bldcya}Cleaning up ${txtrst}"
make clean mrproper
echo -ne '====>\r'
sleep 1
echo -ne '==========>\r'
rm $ZIMAGE
echo -ne '=================>\r'
sleep 1
echo -ne '=========================>\r'
sleep 1
rm -rf ../out
echo -ne '============================>\r'
sleep 1
echo -ne '=================================>\r'
sleep 1
rm -rf ../modules
echo -ne '======================================>\r'
sleep 1
echo -ne '==========================================>\r'
sleep 1
mkdir ../modules
echo -ne '==============================================>\r'
sleep 1
echo -ne '=================================================>\r'
sleep 1
mkdir ../out
echo -ne '========================================================> Done.\r'
echo -ne '\n'
fi

# Moar defines
if [ "NAME" == "Olivier" ]; then
ZIPFILENAME=./AureliusKernel-$TIMESTAMP-htc-$PRODUCT-OFFICIAL.zip
else
ZIPFILENAME=./AureliusKernel-$TIMESTAMP-htc-$PRODUCT-UNOFFICIAL.zip
fi


# Start the fun
res1=$(date +%s.%N)
echo "${bldpnk}Starting the build${txtrst}"
script -q ~/kernel.log -c "make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- $PRODUCT_defconfig;make ARCH=arm CROSS_COMPILE=$TOOLCHAIN- -j$cores"
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
echo "${bldred}Compilation failed! Don't blame Olivier. ${txtrst} ${bldcya}Total time elapsed: ${txtrst}${cya}$(echo "($res2 - $res1) / 60"|bc ) minutes ($(echo "$res2 - $res1"|bc ) seconds) ${txtrst}"
fi
