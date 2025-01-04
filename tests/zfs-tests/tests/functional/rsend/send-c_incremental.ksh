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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that compressed send works correctly with incremental sends.
#
# Strategy:
# 1. Randomly choose either a -i or -I incremental.
# 2. Generate compressed incremental replication streams for a pool, a
#    descendant dataset, and a volume.
# 3. Receive these streams verifying both the contents, and intermediate
#    snapshots are present or absent as appropriate to the -i or -I option.
#

verify_runnable "both"

log_assert "Verify compressed send works with incremental send streams."
log_onexit cleanup_pool $POOL2

typeset opt=$(random_get "-i" "-I")
typeset final dstlist list vol

log_must eval "zfs send -R $POOL@final > $BACKDIR/final"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/final"

function do_checks
{
	log_must cmp_ds_cont $POOL $POOL2
	[[ $opt = "-I" ]] && log_must cmp_ds_subs $POOL $POOL2
	[[ $opt = "-i" ]] && log_mustnot cmp_ds_subs $POOL $POOL2

	[[ $1 != "clean" ]] && return

	cleanup_pool $POOL2
	log_must eval "zfs send -R $POOL@final > $BACKDIR/final"
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/final"
}

if is_global_zone; then
	# Send from the pool root
	final=$(getds_with_suffix $POOL2 @final)
	list="$final $(getds_with_suffix $POOL2 @snapA)"
	list="$list $(getds_with_suffix $POOL2 @snapB)"
	list="$list $(getds_with_suffix $POOL2 @snapC)"

	log_must eval "zfs send -c -R $opt @init $POOL2@final >$BACKDIR/pool"
	log_must destroy_tree $list
	log_must eval "zfs recv -d -F $POOL2 <$BACKDIR/pool"

	dstlist=$(getds_with_suffix $POOL2 @final)
	[[ $final != $dstlist ]] && log_fail "$final != $dstlist"

	do_checks clean

	# Send of a volume
	vol=$POOL2/$FS/vol
	final=$(getds_with_suffix $vol @final)
	log_must eval "zfs send -c -R $opt @init $vol@final >$BACKDIR/vol"
	log_must destroy_tree $vol@snapB $vol@snapC $vol@final
	log_must eval "zfs recv -d -F $POOL2 <$BACKDIR/vol"

	dstlist=$(getds_with_suffix $POOL2/$FS/vol @final)
	[[ $final != $dstlist ]] && log_fail "$final != $dstlist"

	do_checks clean
fi

# Send of a descendant fs
final=$(getds_with_suffix $POOL2/$FS @final)
list="$final $(getds_with_suffix $POOL2/$FS @snapA)"
list="$list $(getds_with_suffix $POOL2/$FS @snapB)"
list="$list $(getds_with_suffix $POOL2/$FS @snapC)"

log_must eval "zfs send -c -R $opt @init $POOL2/$FS@final >$BACKDIR/fs"
log_must destroy_tree $list
log_must eval "zfs recv -d -F $POOL2 <$BACKDIR/fs"

dstlist=$(getds_with_suffix $POOL2/$FS @final)
[[ $final != $dstlist ]] && log_fail "$final != $dstlist"

do_checks

log_pass "Compressed send works with incremental send streams."
