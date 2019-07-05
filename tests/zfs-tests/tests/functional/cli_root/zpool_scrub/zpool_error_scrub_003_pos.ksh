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
# Copyright (c) 2019 by Delphix. All rights reserved.
# Use is subject to license terms.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify error scrub clears the errorlog, if errors no longer exists.
#
# STRATEGY:
#	1. Create a pool and create a 10MB file in it.
#	2. Zinject errors and read using dd to log errors to disk.
#	3. Make sure file name is mentioned in the list of error files.
#	4. Start error scrub and wait for it finish.
#	5. Make sure file name is not mentioned in the list of error files.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	rm -f $TESTDIR/10m_file
}

log_onexit cleanup

log_assert "Verify error scrub clears the errorlog, if errors no longer exists."

log_must mkfile 10m $TESTDIR/10m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -t data -e checksum -f 100 $TESTDIR/10m_file

# create some error blocks
dd if=$TESTDIR/10m_file bs=1M count=1 || true

# sync error blocks to disk
log_must sync_pool $TESTPOOL

log_must eval "zpool status -v $TESTPOOL | grep '10m_file'"
log_must zinject -c all
log_must zpool scrub -e $TESTPOOL

log_mustnot eval "zpool status -v $TESTPOOL | grep '10m_file'"

log_pass "Verify error scrub clears the errorlog, if errors no longer exists."
