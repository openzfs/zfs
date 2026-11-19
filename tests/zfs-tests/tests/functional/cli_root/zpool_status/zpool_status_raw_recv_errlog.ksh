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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool status -v' resolving an error of a dataset that is being raw
# received, with its key loaded, must not leak holds.
#
# The receive keeps the dataset's blocks of dnodes raw, so the bonus
# buffer that zfs_obj_to_path() reads to resolve the entry has no valid
# SA header ("Buffer Header ... != SA_MAGIC"). sa_handle_get() must then
# release the bonus buffer: a leaked hold keeps the objset from being
# evicted, and 'zpool export' waits for it forever.
#
# STRATEGY:
# 1. Create an encrypted dataset with some files on a source pool, and
#    save a raw send stream of a snapshot.
# 2. Receive all but the end of the stream into a second pool (-s -u),
#    and sync it.
# 3. Inject a checksum error into a data block of one of the received
#    files, and scrub, so that the error is logged.
# 4. Load the key of the receiving dataset, and run 'zpool status -v'.
#    It must complete and list the error.
# 5. Feed the rest of the stream: the receive must succeed.
# 6. 'zpool export' must complete.
# 7. Import the pool, load the key and mount the dataset: the files must
#    match the source.
#

verify_runnable "global"

SRCPOOL=testpool_rrsrc
DSTPOOL=testpool_rrdst
SRCVDEV=$TEST_BASE_DIR/vdev_rrsrc
DSTVDEV=$TEST_BASE_DIR/vdev_rrdst
STREAM=$TEST_BASE_DIR/raw_recv_errlog.zsend
GATE=$TEST_BASE_DIR/raw_recv_errlog.gate
STATUS=$TEST_BASE_DIR/raw_recv_errlog.status
PASSPHRASE="password"
typeset -i RECV_PID=0
typeset -i EXPORT_PID=0

function cleanup
{
	if ((EXPORT_PID > 0)) && kill -0 $EXPORT_PID 2>/dev/null; then
		# The pool is wedged, don't hang the cleanup as well.
		log_note "zpool export is still running, pool left as is"
		return
	fi
	zinject -c all >/dev/null 2>&1
	touch $GATE
	if ((RECV_PID > 0)); then
		kill $RECV_PID >/dev/null 2>&1
		wait $RECV_PID
	fi
	destroy_pool $DSTPOOL
	destroy_pool $SRCPOOL
	rm -f $SRCVDEV $DSTVDEV $STREAM $GATE $STATUS
}

ASSERTION="'zpool status -v' on a raw receive with its key leaks no holds"
log_assert "$ASSERTION"
log_onexit cleanup

# 1. The source.
log_must truncate -s $MINVDEVSIZE $SRCVDEV $DSTVDEV
log_must zpool create -f $SRCPOOL $SRCVDEV
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o keylocation=prompt $SRCPOOL/enc"
typeset -i i
for ((i = 1; i <= 200; i++)); do
	dd if=/dev/urandom of=/$SRCPOOL/enc/f$i bs=8k count=1 2>/dev/null ||
	    log_fail "can't create /$SRCPOOL/enc/f$i"
done
log_must dd if=/dev/urandom of=/$SRCPOOL/enc/big bs=1M count=16
# The stream sends the objects in order: damage the lowest-numbered small
# file, which is received before the large file's last blocks.
object=$(ls -i /$SRCPOOL/enc/ | awk '$2 ~ /^f/ {print $1}' | sort -n | head -1)
bigobj=$(ls -i /$SRCPOOL/enc/big | awk '{print $1}')
log_must test $object -lt $bigobj
log_must zfs snapshot $SRCPOOL/enc@snap
log_must eval "zfs send -w $SRCPOOL/enc@snap > $STREAM"

# 2. Receive all but the last 8 MiB (of the large file).
typeset -i part1=$(($(stat_size $STREAM) - 8 * 1048576))
log_must zpool create -f -O compression=off $DSTPOOL $DSTVDEV
rm -f $GATE
{
	head -c $part1 $STREAM
	while [[ ! -f $GATE ]]; do sleep 0.1; done
	tail -c +$((part1 + 1)) $STREAM
} 2>/dev/null | zfs receive -s -u $DSTPOOL/recv &
RECV_PID=$!
typeset -i tries=60
until zfs list $DSTPOOL/recv >/dev/null 2>&1; do
	((tries-- > 0)) || log_fail "receive didn't create $DSTPOOL/recv"
	sleep 1
done
# Wait until the file to damage is received and on disk.
tries=60
while :; do
	log_must zpool sync $DSTPOOL
	zdb -dd $DSTPOOL/recv $object 2>/dev/null | grep -q "ZFS plain file" &&
	    break
	((tries-- > 0)) || log_fail "object $object wasn't received"
	sleep 1
done

# 3. A logged error in that file (raw receives keep the object numbers).
objset=$(get_prop objsetid $DSTPOOL/recv)
log_must zinject -b $(printf "%x:%x:0:0" $objset $object) -e checksum \
    -a $DSTPOOL
log_must zpool scrub -w $DSTPOOL
log_must zinject -c all
log_must eval "zpool status -v $DSTPOOL | grep -q 'Permanent errors'"

# 4. Resolve the error with the key loaded, while the receive runs.
log_must eval "echo $PASSPHRASE | zfs load-key $DSTPOOL/recv"
for ((i = 0; i < 3; i++)); do
	log_must eval "zpool status -v $DSTPOOL > $STATUS"
	log_must grep -q "Permanent errors" $STATUS
done
log_must kill -0 $RECV_PID

# 5. Finish the receive.
log_must touch $GATE
log_must wait $RECV_PID
RECV_PID=0
log_must zfs list -t snapshot $DSTPOOL/recv@snap

# 6. Nothing may keep the objset from being evicted.
zpool export $DSTPOOL &
EXPORT_PID=$!
tries=60
while ((tries-- > 0)) && kill -0 $EXPORT_PID 2>/dev/null; do
	sleep 1
done
if kill -0 $EXPORT_PID 2>/dev/null; then
	log_fail "'zpool export' hung (a hold on $DSTPOOL/recv was leaked)"
fi
log_must wait $EXPORT_PID
EXPORT_PID=0

# 7. The received data is intact.
log_must zpool import -d $TEST_BASE_DIR $DSTPOOL
log_must eval "echo $PASSPHRASE | zfs load-key $DSTPOOL/recv"
log_must zfs mount $DSTPOOL/recv
log_must diff -r /$SRCPOOL/enc /$DSTPOOL/recv

log_pass "$ASSERTION"
