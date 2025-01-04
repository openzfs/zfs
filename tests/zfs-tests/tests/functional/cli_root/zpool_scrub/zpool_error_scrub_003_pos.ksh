#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2019, Delphix. All rights reserved.
# Copyright (c) 2023, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify error scrub clears the errorlog, if errors no longer exist.
#
# STRATEGY:
#	1. Create a pool and create file in it.
#	2. Zinject errors and read using dd to log errors to disk.
#	3. Make sure file name is mentioned in the list of error files.
#	4. Start error scrub and wait for it finish.
#	5. Check scrub ran and errors are still reported.
#	6. Clear corruption and error scrub again.
#	7. Check scrub ran and errors are cleared.
#

verify_runnable "global"

function cleanup
{
	zinject -c all
	rm -f /$TESTPOOL2/$TESTFILE0
	destroy_pool $TESTPOOL2
}

log_onexit cleanup

log_assert "Verify error scrub clears the errorlog, if errors no longer exist."

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -O primarycache=none $TESTPOOL2 $TESTDIR/vdev_a
log_must zfs create $TESTPOOL2/$TESTFS1
typeset file=/$TESTPOOL2/$TESTFS1/$TESTFILE0
log_must dd if=/dev/urandom of=$file bs=2M count=10

lastfs="$(zfs list -r $TESTPOOL2 | tail -1 | awk '{print $1}')"
for i in {1..3}; do
	log_must zfs snap $lastfs@snap$i
	log_must zfs clone $lastfs@snap$i $TESTPOOL2/clone$i
	lastfs="$(zfs list -r $TESTPOOL2/clone$i | tail -1 | awk '{print $1}')"
done

log_must zinject -t data -e checksum -f 100 -a $file
dd if=$file of=/dev/null bs=2M count=10

# Important: sync error log to disk
log_must sync_pool $TESTPOOL2

# Check reported errors
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v | grep '$TESTPOOL2/$TESTFS1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/$TESTFS1@snap1:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1@snap2:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2@snap3:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone3/$TESTFILE0'"

# Check errors are reported if corruption persists
log_must zpool scrub -e -w $TESTPOOL2
log_must eval "zpool status -v | grep 'error blocks'"
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v | grep '$TESTPOOL2/$TESTFS1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/$TESTFS1@snap1:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1@snap2:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2@snap3:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone3/$TESTFILE0'"

# Check errors are cleared
log_must zinject -c all
log_must zpool scrub -e -w $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v | grep 'error blocks'"
log_mustnot eval "zpool status -v | grep '$TESTFILE0'"


log_pass "Verify error scrub clears the errorlog, if errors no longer exist."
