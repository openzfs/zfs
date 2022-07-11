#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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

	[[ -d $import_dir ]] && rm -rf $import_dir
	for file in $VDEV1 $VDEV2 $VDEV3 $VDEV4; do
		[[ -f $file ]] && rm -f $file
	done
}

log_assert "Verify zpool sub-commands which modify state are logged."
log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL)
VDEV1=$mntpnt/vdev1; VDEV2=$mntpnt/vdev2;
VDEV3=$mntpnt/vdev3; VDEV4=$mntpnt/vdev4;

log_must mkfile $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3
log_must mkfile $(($MINVDEVSIZE * 2)) $VDEV4

run_and_verify -p "$MPOOL" "zpool create $MPOOL mirror $VDEV1 $VDEV2"
run_and_verify -p "$MPOOL" "zpool add -f $MPOOL spare $VDEV3"
run_and_verify -p "$MPOOL" "zpool remove $MPOOL $VDEV3"
run_and_verify -p "$MPOOL" "zpool offline $MPOOL $VDEV1"
run_and_verify -p "$MPOOL" "zpool online $MPOOL $VDEV1"
run_and_verify -p "$MPOOL" "zpool attach $MPOOL $VDEV1 $VDEV4"
run_and_verify -p "$MPOOL" "zpool detach $MPOOL $VDEV4"
run_and_verify -p "$MPOOL" "zpool replace -f $MPOOL $VDEV1 $VDEV4"
run_and_verify -p "$MPOOL" "zpool scrub $MPOOL"
run_and_verify -p "$MPOOL" "zpool clear $MPOOL"

# For export and destroy, mimic the behavior of run_and_verify using two
# commands since the history will be unavailable until the pool is imported
# again.
commands=("zpool export $MPOOL" "zpool import -d $mntpnt $MPOOL"
    "zpool destroy $MPOOL" "zpool import -D -f -d $mntpnt $MPOOL")
for i in 0 2; do
	cmd1="${commands[$i]}"
	cmd2="${commands[(($i + 1 ))]}"

	zpool history $MPOOL > $OLD_HISTORY 2>/dev/null
	log_must $cmd1
	log_must $cmd2
	zpool history $MPOOL > $TMP_HISTORY 2>/dev/null
	diff $OLD_HISTORY $TMP_HISTORY | grep "^> " | sed 's/^> //g' > \
	    $NEW_HISTORY
        if is_linux; then
		grep "$(echo "$cmd1" | sed 's/^.*\/\(zpool .*\).*$/\1/')" \
		    $NEW_HISTORY >/dev/null 2>&1 || \
		    log_fail "Didn't find \"$cmd1\" in pool history"
		grep "$(echo "$cmd2" | sed 's/^.*\/\(zpool .*\).*$/\1/')" \
		    $NEW_HISTORY >/dev/null 2>&1 || \
		    log_fail "Didn't find \"$cmd2\" in pool history"
        else
		grep "$(echo "$cmd1" | sed 's/\/usr\/sbin\///g')" \
		    $NEW_HISTORY >/dev/null 2>&1 || \
		    log_fail "Didn't find \"$cmd1\" in pool history"
		grep "$(echo "$cmd2" | sed 's/\/usr\/sbin\///g')" \
		    $NEW_HISTORY >/dev/null 2>&1 || \
		    log_fail "Didn't find \"$cmd2\" in pool history"
        fi
done

run_and_verify -p "$MPOOL" "zpool split $MPOOL ${MPOOL}_split"

import_dir=$TEST_BASE_DIR/import_dir.$$
log_must mkdir $import_dir
log_must cp $STF_SUITE/tests/functional/history/zfs-pool-v4.dat.Z $import_dir
log_must uncompress $import_dir/zfs-pool-v4.dat.Z
upgrade_pool=$(zpool import -d $import_dir | awk '/pool:/ { print $2 }')
log_must zpool import -d $import_dir $upgrade_pool
run_and_verify -p "$upgrade_pool" "zpool upgrade $upgrade_pool"

log_pass "zpool sub-commands which modify state are logged passed. "
