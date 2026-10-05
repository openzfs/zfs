#!/bin/bash
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

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
