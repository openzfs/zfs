#!/bin/ksh -p
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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Setting "readonly", "volthreading" or "volsize" on a volume while another
# process changes its "volmode" must not touch the minor that is being
# removed (openzfs/zfs#19269).
#
# STRATEGY:
# 1. Create a volume.
# 2. Switch its volmode back and forth in one loop, while other loops
#    switch readonly, volthreading and volsize.
# 3. Verify that every loop finishes and the volume is still usable. Some
#    of the changes may fail with EBUSY while the minor is replaced; on a
#    broken system the kernel oopses and the loops hang instead.
#

verify_runnable "global"

typeset vol=$TESTPOOL/racevol
typeset -i duration=30

function cleanup
{
	datasetexists $vol && destroy_dataset $vol
	is_linux && udev_cleanup
}

log_onexit cleanup

log_assert "Setting volume properties races safely with volmode changes"

log_must zfs create -V 64M -o volmode=full $vol
block_device_wait $ZVOL_DEVDIR/$vol

typeset -i end=$(( $(date +%s) + duration ))

(
	while (( $(date +%s) < end )); do
		zfs set volmode=dev $vol
		zfs set volmode=full $vol
	done
) >/dev/null 2>&1 &
typeset volmode_pid=$!

(
	while (( $(date +%s) < end )); do
		zfs set readonly=on $vol
		zfs set readonly=off $vol
	done
) >/dev/null 2>&1 &
typeset readonly_pid=$!

(
	while (( $(date +%s) < end )); do
		zfs set volthreading=off $vol
		zfs set volthreading=on $vol
	done
) >/dev/null 2>&1 &
typeset threading_pid=$!

(
	while (( $(date +%s) < end )); do
		zfs set volsize=128M $vol
		zfs set volsize=64M $vol
	done
) >/dev/null 2>&1 &
typeset volsize_pid=$!

wait $volmode_pid $readonly_pid $threading_pid $volsize_pid

# A minor can still be created, and the volume's own minor works.
log_must zfs set volmode=full readonly=off volsize=64M $vol
log_must zfs snapshot $vol@snap
log_must zfs destroy $vol@snap
block_device_wait $ZVOL_DEVDIR/$vol
log_must dd if=/dev/zero of=$ZVOL_DEVDIR/$vol bs=128k count=8 conv=fsync

log_pass "Setting volume properties races safely with volmode changes"
