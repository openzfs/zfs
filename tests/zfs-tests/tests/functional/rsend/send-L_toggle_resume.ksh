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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# A send without -L splits each 1M block into 128K WRITE records,
# so the receiver stores the file or volume in 128K blocks. A
# receive cut off partway through can be resumed with -L, or with
# -w, which is -Lec for an unencrypted dataset, and the resumed
# stream then has 1M blocks. The receive must keep the blocks it
# already has, and the data must match the source.
#
# Strategy:
# 1. Write an 8M file to a recordsize=1m dataset and snapshot it.
# 2. Send the snapshot without -L and cut the stream so the saved
#    offset falls inside a 1M block.
# 3. Resume with zfs send -L -t. The first WRITE for the resume
#    object must be 1M, and the received file must match.
# 4. Repeat the cut on a fresh receive and resume with
#    zfs send -w -t.
# 5. Write 8M to a volume with volblocksize=1m, snapshot it, cut
#    its send the same way and resume with zfs send -L -t. The
#    received volume must match.
#

verify_runnable "both"

typeset BLK=1048576
typeset sendfs=$POOL/ltr_fs
typeset recvfs=$POOL/ltr_recv
typeset vol=$POOL/ltr_vol
typeset recvvol=$POOL/ltr_recvvol
typeset voldev=$ZVOL_DEVDIR/$vol
typeset recvdev=$ZVOL_DEVDIR/$recvvol
typeset voldata=$BACKDIR/ltr.voldata
typeset volcopy=$BACKDIR/ltr.volcopy
typeset full=$BACKDIR/ltr.full
typeset rest=$BACKDIR/ltr.rest

function cleanup
{
	rm -f $full $rest $voldata $volcopy
	for ds in $sendfs $recvfs $vol $recvvol; do
		datasetexists $ds && destroy_dataset $ds -r
	done
}

function token_val # token name
{
	zstream token $1 | awk -v n="$2:" '$1 == n {print $2}'
}

function first_write_size # stream object
{
	zstream dump -v $1 | awk -v o=$2 '$1 == "WRITE" && $4 == o {
		for (i = 1; i < NF; i++)
			if ($i == "logical_size") {
				print $(i + 2)
				exit
			}
	}'
}

#
# Send the snapshot without -L, cut the stream so the saved offset
# falls inside a 1M block, and resume the receive with the flag.
#
function cut_and_resume # snapshot target flag
{
	typeset snap=$1 target=$2 flag=$3

	log_must eval "zfs send $snap >$full"
	typeset -i size=$(stat_size $full)
	log_mustnot eval "head -c $((size * 45 / 100)) $full |" \
	    "zfs recv -s $target"
	typeset tok=$(get_prop receive_resume_token $target)
	log_must test "$tok" != "-"
	typeset obj=$(token_val $tok object)
	typeset off=$(token_val $tok offset)
	log_must test -n "$off"
	log_note "zfs send $flag -t for $snap: obj $obj off $off"
	log_must test $((off % BLK)) -ne 0

	log_must eval "zfs send $flag -t $tok >$rest"
	log_must test "$(first_write_size $rest $obj)" = "$BLK"
	log_must eval "zfs recv -s $target <$rest"
}

log_assert "A send started without -L can be resumed with -L or -w"
log_onexit cleanup

log_must zfs create -o recordsize=1m $sendfs
typeset sendmnt=$(get_prop mountpoint $sendfs)
log_must file_write -o create -f $sendmnt/file -b $BLK -c 8 -d R
log_must zfs snapshot $sendfs@snap

for flag in -L -w; do
	cut_and_resume $sendfs@snap $recvfs $flag
	typeset recvmnt=$(get_prop mountpoint $recvfs)
	log_must diff $sendmnt/file $recvmnt/file
	log_must_busy zfs destroy -r $recvfs
done

log_must zfs create -V 8m -o volblocksize=1m $vol
block_device_wait $voldev
log_must file_write -o create -f $voldata -b $BLK -c 8 -d R
log_must dd if=$voldata of=$voldev bs=$BLK conv=fsync
log_must zfs snapshot $vol@snap

cut_and_resume $vol@snap $recvvol -L
block_device_wait $recvdev
log_must dd if=$recvdev of=$volcopy bs=$BLK count=8
log_must cmp $voldata $volcopy

log_pass "A send started without -L can be resumed with -L or -w"
