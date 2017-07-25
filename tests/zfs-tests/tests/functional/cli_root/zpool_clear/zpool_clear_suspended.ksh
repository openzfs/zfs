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
# Verify 'zpool clear' works on suspended pools.
#
# STRATEGY:
# 1. Create a pool.
# 2. Suspend the pool.
# 3. Verify various operations fail on a suspended pool.
# 4. Verify we can 'zpool clear' the suspended pool.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL1
	rm -f $TESTDIR/file.*
}

#
# Wait $pool to be suspended
#
function wait_pool_suspended # pool
{
	typeset poolname="$1"

	for i in {1..3}; do
		check_pool_status "$poolname" "state" "UNAVAIL" && return 0
		sleep 1
	done
	return 1
}

log_assert "Verify 'zpool clear' works on suspended pools."
log_onexit cleanup

typeset status
typeset -i scrub_pid
typeset -i retval

# 1. Create a pool.
log_must truncate -s $FILESIZE $TESTDIR/file.{1,2,3}
log_must zpool create $TESTPOOL1 raidz $TESTDIR/file.*

# 2. Suspend the pool
# 2.1 zinject read errors on every device
for i in {1..3}; do
	log_must zinject -d $TESTDIR/file.$i -e nxio -T read $TESTPOOL1
done
# 2.2 scrub the pool
zpool scrub $TESTPOOL1 &
scrub_pid=$!
# 2.3 verify the pool is suspended
log_must wait_pool_suspended $TESTPOOL1

# 3. Verify various operations fail on a suspended pool.
log_mustnot zpool set comment="No I/O on suspended pools" $TESTPOOL1
log_mustnot zpool reopen $TESTPOOL1
log_mustnot zpool scrub $TESTPOOL1

# 4. Verify we can 'zpool clear' the suspended pool.
log_must zinject -c all
log_must zpool clear $TESTPOOL1
status="$(get_pool_prop health $TESTPOOL1)"
if [[ "$status" != 'ONLINE' ]]; then
	log_fail "Pool $TESTPOOL1 is not ONLINE ($status)"
fi
wait $scrub_pid
retval=$?
if [[ $retval -ne 0 ]]; then
	log_fail "'zpool scrub' exit status is not 0 ($retval)"
fi

log_pass "'zpool clear' works on suspended pools."
