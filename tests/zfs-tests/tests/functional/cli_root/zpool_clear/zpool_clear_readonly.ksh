#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
