#!/bin/bash

#exit code 1 means ZEVO is neither installed nor just uninstalled without reboot

echo "ZEVO files check"
ls /System/Library/Extensions/ZFSDriver.kext/ &>/dev/null && exit 0
ls /System/Library/Extensions/ZFSFilesystem.kext/ &>/dev/null && exit 0
ls /Library/LaunchDaemons/com.getgreenbytes* &>/dev/null && exit 0

echo "ZEVO launchctl check"
/bin/launchctl list | grep greenbytes &>/dev/null
[ $? -eq 0 ] && exit 0

echo "ZEVO kextstat check"
/usr/sbin/kextstat | grep greenbytes &>/dev/null
[ $? -eq 0 ] && exit 0

exit 1
