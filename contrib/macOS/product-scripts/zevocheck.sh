#!/bin/bash

#exit code 1 means ZEVO is neither installed nor just uninstalled without reboot

echo "ZEVO files check"
&>/dev/null ls /System/Library/Extensions/ZFSDriver.kext/ && exit 0
&>/dev/null ls /System/Library/Extensions/ZFSFilesystem.kext/ && exit 0
&>/dev/null ls /Library/LaunchDaemons/com.getgreenbytes* && exit 0

echo "ZEVO launchctl check"
/bin/launchctl list | grep greenbytes &>/dev/null
[ $? -eq 0 ] && exit 0

echo "ZEVO kextstat check"
/usr/sbin/kextstat | grep greenbytes &>/dev/null
[ $? -eq 0 ] && exit 0

exit 1
