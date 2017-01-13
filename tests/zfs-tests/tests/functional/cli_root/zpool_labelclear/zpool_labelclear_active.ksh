#!/bin/ksh -p
#
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
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_labelclear/labelclear.cfg

# DESCRIPTION:
# Check that zpool labelclear will refuse to clear the label
# (with or without -f) on any vdevs of the imported pool.
#
# STRATEGY:
# 1. Create the pool with log device.
# 2. Try clearing the label on data and log devices.
# 3. Add auxilary (cache/spare) vdevs.
# 4. Try clearing the label on auxilary vdevs.
# 5. Check that zpool labelclear will return non-zero and
#    labels are intact.

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup
log_assert "zpool labelclear will fail on all vdevs of imported pool"

# Create simple pool, skip any mounts
log_must zpool create -O mountpoint=none -f $TESTPOOL $disk1 log $disk2

# Check that labelclear [-f] will fail on ACTIVE pool vdevs
log_mustnot $LABELCLEAR $disk1
log_must $LABELREAD $disk1
log_mustnot $LABELCLEAR -f $disk1
log_must $LABELREAD $disk1
log_mustnot $LABELCLEAR $disk2
log_must $LABELREAD $disk2
log_mustnot $LABELCLEAR -f $disk2
log_must $LABELREAD $disk2

# Add a cache/spare to the pool, check that labelclear [-f] will fail
# on the vdev and will succeed once it's removed from pool config
for vdevtype in "cache" "spare"; do
	log_must zpool add $TESTPOOL $vdevtype $disk3
	log_mustnot $LABELCLEAR $disk3
	log_must $LABELREAD $disk3
	log_mustnot $LABELCLEAR -f $disk3
	log_must $LABELREAD $disk3
	log_must zpool remove $TESTPOOL $disk3
	log_must $LABELCLEAR $disk3
	log_mustnot $LABELREAD $disk3
done

log_pass "zpool labelclear will fail on all vdevs of imported pool"
