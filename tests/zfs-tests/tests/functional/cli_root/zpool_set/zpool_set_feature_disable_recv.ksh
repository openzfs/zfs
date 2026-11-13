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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	"zpool set feature@<name>=disabled" is refused for large_dnode and
#	zstd_compress while a receive whose stream has their flag is in
#	progress, although no large dnode or zstd block has arrived.  Once
#	the receive has ended, completed or failed, the disable succeeds, and
#	resuming a failed receive is then refused.
#
# STRATEGY:
#	1. Create a source pool, and a destination pool on which large_dnode
#	   and zstd_compress are enabled but not active.
#	2. On a source file system, activate large_dnode with a 1K dnode that
#	   is removed again, and zstd_compress by setting compression=zstd,
#	   with its data written uncompressed.  A compressed stream of it has
#	   both flags, but no large dnode or zstd block.
#	3. Receive the stream through a FIFO that stops part way, so that the
#	   receive waits in the kernel: disabling either feature must fail.
#	   Write the rest: once the receive completes, both features are
#	   still only enabled, and disabling them must succeed.
#	4. Enable them again and start a resumable receive the same way:
#	   disabling must fail.  Close the FIFO part way: once the receive has
#	   failed, disabling must succeed, and resuming must fail until the
#	   features are enabled again.
#

verify_runnable "global"

SRC=$TESTPOOL1
DST=$TESTPOOL2
SRC_VDEV=$TEST_BASE_DIR/feature_disable_recv_src
DST_VDEV=$TEST_BASE_DIR/feature_disable_recv_dst
STREAM=$TEST_BASE_DIR/feature_disable_recv.stream
FIFO=$TEST_BASE_DIR/feature_disable_recv.fifo
FEATURES="large_dnode zstd_compress"
HEAD_KB=64
recv_pid=

function cleanup
{
	exec 3>&-
	[[ -n "$recv_pid" ]] && wait $recv_pid
	poolexists $SRC && destroy_pool $SRC
	poolexists $DST && destroy_pool $DST
	rm -f $SRC_VDEV $DST_VDEV $STREAM $FIFO
}

function check_features # value
{
	typeset f val

	for f in $FEATURES; do
		val=$(get_pool_prop feature@$f $DST)
		[[ "$val" == "$1" ]] ||
		    log_fail "feature@$f is '$val', expected '$1'"
	done
}

function set_features # value
{
	typeset f

	for f in $FEATURES; do
		log_must zpool set feature@$f=$1 $DST
	done
}

#
# Start "zfs receive <args>" on the FIFO and give it the first HEAD_KB of
# the stream, then wait until it has created dataset, in the sync task
# that checks the stream's feature flags.
#
function start_recv # dataset receive-args...
{
	typeset ds=$1
	typeset -i i=0
	shift

	zfs receive "$@" < $FIFO &
	recv_pid=$!
	exec 3>$FIFO
	dd if=$STREAM bs=1k count=$HEAD_KB >&3 2>/dev/null ||
	    log_fail "cannot write the start of the stream"
	while ! datasetexists $ds; do
		((i++ < 60)) || log_fail "the receive into $ds did not start"
		sleep 1
	done
	kill -0 $recv_pid || log_fail "the receive into $ds ended early"
}

function check_disable_refused
{
	typeset f

	for f in $FEATURES; do
		log_mustnot_expect "busy" zpool set feature@$f=disabled $DST
	done
	check_features enabled
}

log_assert "'zpool set feature@<name>=disabled' is refused while a receive" \
    "whose stream may use the feature is in progress"
log_onexit cleanup

# 1. Pools
log_must truncate -s $MINVDEVSIZE $SRC_VDEV $DST_VDEV
log_must zpool create -f $SRC $SRC_VDEV
log_must zpool create -f $DST $DST_VDEV
check_features enabled

# 2. A stream with both flags and none of their dnodes or blocks
log_must zfs create -o compression=off $SRC/fs
mntpnt=$(get_prop mountpoint $SRC/fs)
log_must dd if=/dev/urandom of=$mntpnt/data bs=1M count=1
log_must zfs set dnodesize=1k $SRC/fs
log_must touch $mntpnt/large_dnode
sync_pool $SRC
log_must rm $mntpnt/large_dnode
log_must zfs set dnodesize=legacy $SRC/fs
log_must zfs set compression=zstd $SRC/fs
log_must zfs set compression=off $SRC/fs
sync_pool $SRC
for f in $FEATURES; do
	log_must test "$(get_pool_prop feature@$f $SRC)" == "active"
done
log_must zfs snapshot $SRC/fs@snap
log_must eval "zfs send -c $SRC/fs@snap > $STREAM"
(( $(stat_size $STREAM) > HEAD_KB * 1024 )) ||
    log_fail "the stream is not larger than $HEAD_KB KiB"
log_must mkfifo $FIFO

# 3. A receive that completes
start_recv $DST/recv1 $DST/recv1
check_disable_refused
dd if=$STREAM bs=1k skip=$HEAD_KB >&3 2>/dev/null ||
    log_fail "cannot write the rest of the stream"
exec 3>&-
wait $recv_pid || log_fail "the receive into $DST/recv1 failed"
recv_pid=
log_must datasetexists $DST/recv1@snap
check_features enabled
set_features disabled
check_features disabled

# 4. A resumable receive that fails
set_features enabled
start_recv $DST/recv2 -s $DST/recv2
check_disable_refused
exec 3>&-
wait $recv_pid && log_fail "the receive into $DST/recv2 completed"
recv_pid=
token=$(get_prop receive_resume_token $DST/recv2)
log_must test -n "$token" -a "$token" != "-"
set_features disabled
check_features disabled
log_mustnot_expect "upgraded" eval \
    "zfs send -t $token | zfs receive -s $DST/recv2"
set_features enabled
log_must eval "zfs send -t $token | zfs receive -s $DST/recv2"
log_must datasetexists $DST/recv2@snap
check_features enabled

log_pass "'zpool set feature@<name>=disabled' is refused while a receive" \
    "whose stream may use the feature is in progress"
