#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
# 'zpool split' should work with whole-disk devices.
#
# STRATEGY:
# 1. Create a mirror with a whole-disk device
# 2. Verify 'zpool split' works and successfully split the mirror
# 3. Cleanup and create the same mirror
# 4. Verify 'zpool split' using the other device
#

verify_runnable "both"

if is_linux; then
	# Add one 512b spare device (4Kn would generate IO errors on replace)
	# NOTE: must be larger than other "file" vdevs and minimum SPA devsize:
	# add 32m of fudge
	load_scsi_debug $(($SPA_MINDEVSIZE/1024/1024+32)) 1 1 1 '512b'
else
	log_unsupported "scsi debug module unsupported"
fi

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	unload_scsi_debug
	rm -fd "$FILE_DEVICE" "$ALTROOT"
}

function setup_mirror
{
	# NOTE: "-f" is required to create a mixed (file and disk device) mirror
	log_must truncate -s $SPA_MINDEVSIZE $FILE_DEVICE
	log_must zpool create -f $TESTPOOL mirror $FILE_DEVICE $DISK_DEVICE
	# NOTE: verify disk is actually a "whole-disk" device
	log_must test  "$(zdb -PC $TESTPOOL | grep -c 'whole_disk: 1')" == 1
}

log_assert "'zpool split' should work with whole-disk devices"
log_onexit cleanup

FILE_DEVICE="$TEST_BASE_DIR/file-device"
DISK_DEVICE="$(get_debug_device)"
ALTROOT="$TEST_BASE_DIR/altroot-$TESTPOOL2"

# 1. Create a mirror with a whole-disk device
setup_mirror

# 2. Verify 'zpool split' works and successfully split the mirror
log_must zpool split -R "$ALTROOT" $TESTPOOL $TESTPOOL2 $DISK_DEVICE
log_must check_vdev_state $TESTPOOL $FILE_DEVICE "ONLINE"
log_must check_vdev_state $TESTPOOL2 $DISK_DEVICE "ONLINE"

# 3. Cleanup and create the same mirror
destroy_pool $TESTPOOL
destroy_pool $TESTPOOL2
setup_mirror

# 4. Verify 'zpool split' using the other device
log_must zpool split -R "$ALTROOT" $TESTPOOL $TESTPOOL2 $FILE_DEVICE
log_must check_vdev_state $TESTPOOL $DISK_DEVICE "ONLINE"
log_must check_vdev_state $TESTPOOL2 $FILE_DEVICE "ONLINE"

log_pass "'zpool split' works with whole-disk devices"
