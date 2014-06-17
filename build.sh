#!/bin/bash

#Stop script if something is broken
set -e

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
VERSION="1.1"
TIMESTAMP=`date +"%Y%m%d"`
TIMESTAMP_INFO=`date +"%A, %d %B %Y"`

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

echo "${bldcya}How many threads do you want to use for the compilation (enter a number)? ${txtrst}"
read cores

echo "${bldcya}What device do you want to build for? [1/2/3/4] ${txtrst}"
echo "${bldcya} 1 - HTC Aria ${txtrst}"
echo "${bldcya} 2 - HTC ChaCha ${txtrst}"
echo "${bldcya} 3 - HTC Salsa ${txtrst}"
echo "${bldcya} 4 - HTC Wildfire S ${txtrst}"
read prod
if [ "$prod" = "1" -o "$answer" = "4" ]; then
export PRODUCT=cyanogen_msm7227
export PRODUCTCODENAME=msm7227
export PRODUCTNAME=HTC Aria and Wildfire S
fi
if [ "$prod" = "2" ]; then
export PRODUCT=chacha
export PRODUCTCODENAME=$PRODUCT
export PRODUCTNAME=HTC ChaCha
fi
if [ "$prod" = "3" ]; then
export PRODUCT=icong
export PRODUCTCODENAME=$PRODUCT
export PRODUCTNAME=HTC Salsa
fi

ZIPFILENAME=./ak-$VERSION-$TIMESTAMP-$PRODUCTCODENAME.zip

echo "${bldcya}Do you want to build a flashable ZIP as well? ${txtrst} [y/n]"
read anykernel

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
echo "${bldred}Compilation failed! Don't blame me, $NAME. ${txtrst}"
echo "${bldred}You may want to try {txtrst} ${bldpur}make -j$cores $PRODUCT_defconfig${txtrst}!"
echo "${bldblu}If that doesn't work either, contact Olivier please.${txtrst}!"
echo "${bldcya}Total time elapsed: ${txtrst}${cya}$(echo "($res2 - $res1) / 60"|bc ) minutes ($(echo "$res2 - $res1"|bc ) seconds) ${txtrst}"
fi

if [ "$anykernel" == "y" ]; then
echo "Creating a flashable zip"
cp arch/arm/boot/zImage AnyKernel/kernel
cp ../modules/* AnyKernel/system/lib/modules
rm AnyKernel/system/lib/modules/placeholder
cd AnyKernel

function make_updater_script(){

#misc stuff
cat << EOF > updater-script
ui_print("================================");
ui_print("         AureliusKernel         ");
ui_print("");
ui_print("        for $PRODUCTNAME       ");
ui_print("");
ui_print("           by Olivier           ");
ui_print("");
ui_print("  Build date: $TIMESTAMP_INFO    ");
ui_print("================================");
ui_print("");
ui_print("");
ui_print("");
EOF

#extract files
cat << EOF >> updater-script
ui_print("|> Mounting...");
mount("yaffs2", "MTD", "system", "/system");
ui_print("|> Extracting system files...");
package_extract_dir("system", "/system");
unmount("/system");
ui_print("|> Extracting kernel files...");
package_extract_dir("kernel", "/tmp");
ui_print("|> Installing kernel...");
set_perm(0, 0, 0777, "/tmp/dump_image");
set_perm(0, 0, 0777, "/tmp/mkbootimg.sh");
set_perm(0, 0, 0777, "/tmp/mkbootimg");
set_perm(0, 0, 0777, "/tmp/unpackbootimg");
run_program("/tmp/dump_image", "boot", "/tmp/boot.img");
run_program("/tmp/unpackbootimg", "/tmp/boot.img", "/tmp/");
run_program("/tmp/mkbootimg.sh");
write_raw_image("/tmp/newboot.img", "boot");
ui_print("Done. Enjoy!");

EOF

mv -f updater-script ./META-INF/com/google/android/updater-script

}

make_updater_script
zip -r $ZIPFILENAME ./META-INF
zip -r $ZIPFILENAME ./system
zip -r $ZIPFILENAME ./kernel
mv *.zip ../../out;
echo "Package complete: ../out/$ZIPFILENAME"
fi
