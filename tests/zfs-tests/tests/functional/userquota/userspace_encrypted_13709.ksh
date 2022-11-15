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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# Avoid allowing #11294/#13709 to recur a third time.
#
# So we hardcode a copy of a pool with this bug, try unlocking it,
# and fail on error. Simple.

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/userspace_13709"
POOLNAME="testpool_13709"

log_assert "ZFS should be able to unlock pools with #13709's failure mode"

log_must bzcat $STF_SUITE/tests/functional/userquota/13709_reproducer.bz2 > $FILEDEV

log_must zpool import -d $FILEDEV $POOLNAME

echo -e 'password\npassword\n' | log_must zfs mount -al

# Cleanup
cleanup

log_pass "#13709 not happening here"
