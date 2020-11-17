#!/bin/ksh -p
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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs send' should be able to send (big) sparse files correctly.
#
# STRATEGY:
# 1. Create sparse files of various size
# 2. Snapshot and send these sparse files
# 3. Verify these files are received correctly and we don't trigger any issue
#    like the one described in https://github.com/openzfs/zfs/pull/6760
#

verify_runnable "both"

function cleanup
{
        datasetexists $SENDFS && log_must zfs destroy -r $SENDFS
        datasetexists $RECVFS && log_must zfs destroy -r $RECVFS
}

#
# Write 1 random byte at $offset of "source" file in $sendfs dataset
# Snapshot and send $sendfs dataset to $recvfs
# Compare the received file with its source
#
function write_compare_files # <sendfs> <recvfs> <offset>
{
	typeset sendfs="$1"
	typeset recvfs="$2"
	typeset offset="$3"

	# create source filesystem
	log_must zfs create $sendfs
	# write sparse file
	sendfile="$(get_prop mountpoint $sendfs)/data.bin"
	log_must dd if=/dev/urandom of=$sendfile bs=1 count=1 seek=$offset
	# send/receive the file
	log_must zfs snapshot $sendfs@snap
	log_must eval "zfs send $sendfs@snap | zfs receive $recvfs"
	# compare sparse files
	recvfile="$(get_prop mountpoint $recvfs)/data.bin"
	log_must cmp $sendfile $recvfile $offset $offset
	sendsz=$(stat_size $sendfile)
	recvsz=$(stat_size $recvfile)
	if [[ $sendsz -ne $recvsz ]]; then
		log_fail "$sendfile ($sendsz) and $recvfile ($recvsz) differ."
	fi
	# cleanup
	log_must zfs destroy -r $sendfs
	log_must zfs destroy -r $recvfs
}

log_assert "'zfs send' should be able to send (big) sparse files correctly."
log_onexit cleanup

SENDFS="$TESTPOOL/sendfs"
RECVFS="$TESTPOOL/recvfs"
OFF_T_MAX="$(echo '2 ^ 40 * 8 - 1' | bc)"

for i in {1..60}; do
	offset=$(echo "2 ^ $i" | bc)
	[[ is_32bit ]] && [[ $offset -ge $OFF_T_MAX ]] && continue;
	write_compare_files $SENDFS $RECVFS $offset
done

log_pass "'zfs send' sends (big) sparse files correctly."
