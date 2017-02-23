#!/bin/ksh

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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb will not produce redundant dumps of configurations
#
# Strategy:
# 1. Create a pool with two vdevs
# 2. Copy label 1 from the first vdev to the second vdev
# 3. Collect zdb -l output for both vdevs
# 4. Verify that the correct number of configs is dumped for each
#

log_assert "Verify zdb does not produce redundant dumps of configurations"
log_onexit cleanup

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

verify_runnable "global"
verify_disk_count "$DISKS" 2

config_count=(1 2)
set -A DISK $DISKS

default_mirror_setup_noexit $DISKS
log_must $DD if=/dev/${DISK[0]} of=/dev/${DISK[1]} bs=1K count=256 conv=notrunc

for x in 0 1 ; do
	config_count=$($ZDB -l $DEV_RDSKDIR/${DISK[$x]} | $GREP -c features_for_read)
	(( $? != 0)) && log_fail "failed to get config_count from DISK[$x]"
	log_note "vdev $x: message_count $config_count"
	[ $config_count -ne ${config_count[$x]} ] && \
		log_fail "zdb produces an incorrect number of configuration dumps."
done

cleanup

log_pass "zdb produces unique dumps of configurations."
