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
#
# Expected to be run from the root of the source tree, as root;
# ./scripts/load_macos.sh
#
# Copies compiled zfs.kext to /tmp/ and prepares the requirements
# for load.
#

rsync -ar module/os/macos/zfs.kext/ /tmp/zfs.kext/

chown -R root:wheel /tmp/zfs.kext

kextload -v /tmp/zfs.kext || kextutil /tmp/zfs.kext

# log stream --source --predicate 'sender == "zfs"' --style compact
