#!/bin/bash
#
# Expected to be run from the root of the source tree, as root;
# ./scripts/load_macos.sh
#
# Copies compiled zfs.kext to /tmp/ and prepares the requirements
# for load.
#

rsync -ar module/os/macos/zfs/zfs.kext/ /tmp/zfs.kext/

chown -R root:wheel /tmp/zfs.kext

kextload -v /tmp/zfs.kext || kextutil /tmp/zfs.kext

# log stream --source --predicate 'sender == "zfs"' --style compact

