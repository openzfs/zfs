#!/bin/bash
#
# Submit installer to Microsoft to be signed with EV certificate.
#
# 1: Compile project with codesign
# 2: Run this script with no arguments
#    it will read the openzfs.ddf file and create disk1/openzfs.cab
# 3: Go to URL and add "New Hardware Submission" upload openzfs.cab
# 4: Wait for the signature, and "Download signed files"
#    Can be hard to find, click small "More" on the upload line.
# 5: Run script again with downloaded filename as argument.
#    MS signed binaries are copied into out/ directory, do not compile again
# 6: Run Inno Setup and produce installer.
#
# 20231212 lundman
#
# This script should probably be made less "lundman only".

# No args?
CERT=20564273046A323E710BFDC5C9CC22E7392F8049

WDK_BASE_PATH="/c/Program Files (x86)/Windows Kits/10/bin"
LATEST_VERSION=$(ls "$WDK_BASE_PATH" | grep "..\..\......\.." | tail -n 1)
SIGNTOOL="$WDK_BASE_PATH/$LATEST_VERSION/x64/signtool.exe"

if [ $# -eq 0 ]; then

	makecab -f contrib/windows/openzfs.ddf

        "$SIGNTOOL" sign -v -as -fd sha256 -td sha256 -sha1 $CERT -tr http://ts.ssl.com disk1/OpenZFS.cab

	echo "Now go to https://partner.microsoft.com/en-us/dashboard/hardware/driver/New#?productId=14183878782569539"
	echo "Sign it, and download new package, run:"
	echo " $0 Signed_XXXX.zip "

	exit 0
fi


rm -rf drivers
unzip $1

cp drivers/OpenZFS/OpenZFS.Inf out/build/x64-Debug/module/os/windows/driver/
cp drivers/OpenZFS/OpenZFS.cat out/build/x64-Debug/module/os/windows/driver/
cp drivers/OpenZFS/OpenZFS.man out/build/x64-Debug/module/os/windows/driver/
cp drivers/OpenZFS/OpenZFS.sys out/build/x64-Debug/module/os/windows/driver/
cp drivers/OpenZFS/OpenZVOL.sys out/build/x64-Debug/module/os/windows/zvol/driver/
cp drivers/OpenZFS/OpenZVOL.cat out/build/x64-Debug/module/os/windows/zvol/driver/
cp drivers/OpenZFS/OpenZVOL.Inf out/build/x64-Debug/module/os/windows/zvol/driver/

echo "Populating symstore ... "
contrib/windows/symstore.sh

echo ""
echo "Now run Inno Setup to produce installer"
