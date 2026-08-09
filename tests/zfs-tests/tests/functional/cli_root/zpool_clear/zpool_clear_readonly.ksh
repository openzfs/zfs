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

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
# Verify 'zpool clear' cannot be used on readonly pools.
#
# STRATEGY:
# 1. Create a pool.
# 2. Export the pool and import it readonly.
# 3. Verify 'zpool clear' on the pool (and each device) returns an error.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $TESTDIR/file.*
}

log_assert "Verify 'zpool clear' cannot be used on readonly pools."
log_onexit cleanup

# 1. Create a pool.
log_must truncate -s $FILESIZE $TESTDIR/file.{1,2,3}
log_must zpool create $TESTPOOL1 raidz $TESTDIR/file.*

# 2. Export the pool and import it readonly.
log_must zpool export $TESTPOOL1
log_must zpool import -d $TESTDIR -o readonly=on $TESTPOOL1
if [[ "$(get_pool_prop readonly $TESTPOOL1)" != 'on' ]]; then
	log_fail "Pool $TESTPOOL1 was not imported readonly."
fi

# 3. Verify 'zpool clear' on the pool (and each device) returns an error.
log_mustnot zpool clear $TESTPOOL1
for i in {1..3}; do
	# Device must be online
	log_must check_state $TESTPOOL1 $TESTDIR/file.$i 'online'
	# Device cannot be cleared if the pool was imported readonly
	log_mustnot zpool clear $TESTPOOL1 $TESTDIR/file.$i
done

log_pass "'zpool clear' fails on readonly pools as expected."
