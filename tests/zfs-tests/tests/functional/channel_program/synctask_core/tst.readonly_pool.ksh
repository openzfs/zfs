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

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#	On a pool imported read-only, a read-only channel program (zfs
#	program -n) runs, while a channel program in syncing context fails
#	with "pool is read-only" and changes nothing.
#

verify_runnable "global"

pool=cp_readonly
dir=$TEST_BASE_DIR/cp_readonly.$$

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $dir
}

log_onexit cleanup

log_assert "Read-only channel programs run on a read-only pool"

log_must mkdir -p $dir
log_must truncate -s $MINVDEVSIZE $dir/vdev
log_must zpool create -o cachefile=none $pool $dir/vdev
log_must zfs create -o com.example:prop=value $pool/fs
log_must zfs snapshot $pool/fs@snap
log_must zpool export $pool
log_must zpool import -o readonly=on -d $dir $pool

cat > $dir/read.zcp <<-EOF
	local fs = (...)["argv"][1]
	local snaps = {}
	for s in zfs.list.snapshots(fs) do
		snaps[#snaps + 1] = s
	end
	return {prop = zfs.get_prop(fs, "com.example:prop"), snaps = snaps}
EOF
cat > $dir/sync.zcp <<-EOF
	return zfs.sync.snapshot((...)["argv"][1] .. "@new")
EOF

log_must eval "zfs program -n $pool $dir/read.zcp $pool/fs > $dir/out 2>&1"
log_must grep -q "prop: 'value'" $dir/out
log_must grep -q "'$pool/fs@snap'" $dir/out

log_mustnot eval "zfs program $pool $dir/read.zcp $pool/fs > $dir/out 2>&1"
log_must grep -q "pool is read-only" $dir/out
log_mustnot eval "zfs program $pool $dir/sync.zcp $pool/fs > $dir/out 2>&1"
log_must grep -q "pool is read-only" $dir/out
log_mustnot datasetexists $pool/fs@new

log_pass "Read-only channel programs run on a read-only pool"
