#!/usr/bin/env bash

######################################################################
# 4) configure and build openzfs modules
######################################################################
echo "Build modules in QEMU machine"

# Bring our VM back up and copy over ZFS source
.github/workflows/scripts/qemu-prepare-for-build.sh

ssh zfs@vm0 '$HOME/zfs/.github/workflows/scripts/qemu-4-build-vm.sh' $@
