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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Create a scenario to verify the following zpool subcommands are logged.
#	create, destroy, add, remove, offline, online, attach, detach, replace,
#	scrub, export, import, clear, upgrade.
#
# STRATEGY:
#	1. Create three virtual disk files and create a mirror.
#	2. Run and verify pool commands, with special casing for destroy/export.
#	3. Import a pool and upgrade it, verifying 'upgrade' was logged.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $MPOOL
	destroy_pool $upgrade_pool

	[[ -d $import_dir ]] && $RM -rf $import_dir
	for file in $VDEV1 $VDEV2 $VDEV3 $VDEV4; do
		[[ -f $file ]] && $RM -f $file
	done
}

log_assert "Verify zpool sub-commands which modify state are logged."
log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL)
(( $? != 0)) && log_fail "get_prop($TESTPOOL mountpoint)"
VDEV1=$mntpnt/vdev1; VDEV2=$mntpnt/vdev2;
VDEV3=$mntpnt/vdev3; VDEV4=$mntpnt/vdev4;

log_must $MKFILE 64m $VDEV1 $VDEV2 $VDEV3
log_must $MKFILE 100m $VDEV4

run_and_verify -p "$MPOOL" "$ZPOOL create $MPOOL mirror $VDEV1 $VDEV2"
run_and_verify -p "$MPOOL" "$ZPOOL add -f $MPOOL spare $VDEV3"
run_and_verify -p "$MPOOL" "$ZPOOL remove $MPOOL $VDEV3"
run_and_verify -p "$MPOOL" "$ZPOOL offline $MPOOL $VDEV1"
run_and_verify -p "$MPOOL" "$ZPOOL online $MPOOL $VDEV1"
run_and_verify -p "$MPOOL" "$ZPOOL attach $MPOOL $VDEV1 $VDEV4"
run_and_verify -p "$MPOOL" "$ZPOOL detach $MPOOL $VDEV4"
run_and_verify -p "$MPOOL" "$ZPOOL replace -f $MPOOL $VDEV1 $VDEV4"
run_and_verify -p "$MPOOL" "$ZPOOL scrub $MPOOL"
run_and_verify -p "$MPOOL" "$ZPOOL clear $MPOOL"

# For export and destroy, mimic the behavior of run_and_verify using two
# commands since the history will be unavailable until the pool is imported
# again.
commands=("$ZPOOL export $MPOOL" "$ZPOOL import -d $mntpnt $MPOOL"
    "$ZPOOL destroy $MPOOL" "$ZPOOL import -D -f -d $mntpnt $MPOOL")
for i in 0 2; do
	cmd1="${commands[$i]}"
	cmd2="${commands[(($i + 1 ))]}"

	$ZPOOL history $MPOOL > $OLD_HISTORY 2>/dev/null
	log_must $cmd1
	log_must $cmd2
	$ZPOOL history $MPOOL > $TMP_HISTORY 2>/dev/null
	$DIFF $OLD_HISTORY $TMP_HISTORY | $GREP "^> " | $SED 's/^> //g' > \
	    $NEW_HISTORY
	$GREP "$($ECHO "$cmd1" | $SED 's/\/usr\/sbin\///g')" $NEW_HISTORY \
	    >/dev/null 2>&1 || log_fail "Didn't find \"$cmd1\" in pool history"
	$GREP "$($ECHO "$cmd2" | $SED 's/\/usr\/sbin\///g')" $NEW_HISTORY \
	    >/dev/null 2>&1 || log_fail "Didn't find \"$cmd2\" in pool history"
done

run_and_verify -p "$MPOOL" "$ZPOOL split $MPOOL ${MPOOL}_split"

import_dir=/var/tmp/import_dir.$$
log_must $MKDIR $import_dir
log_must $CP $STF_SUITE/tests/functional/history/zfs-pool-v4.dat.Z $import_dir
log_must $UNCOMPRESS $import_dir/zfs-pool-v4.dat.Z
upgrade_pool=$($ZPOOL import -d $import_dir | $GREP "pool:" | $AWK '{print $2}')
log_must $ZPOOL import -d $import_dir $upgrade_pool
run_and_verify -p "$upgrade_pool" "$ZPOOL upgrade $upgrade_pool"

log_pass "zpool sub-commands which modify state are logged passed. "
