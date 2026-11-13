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
# A send without -L splits each 1M block into 128K WRITE records.
# A receive cut off partway through such a block saves a resume
# offset inside the block. The resumed send must start at that
# offset, and the received files must match the source.
#
# Strategy:
# 1. Write a 6M file and a file with one 293K block to a
#    recordsize=1m dataset.
# 2. Send the snapshot without -L.
# 3. Cut the stream at five points, the last two near the end of
#    the stream, and receive with -s.
# 4. Resume with zfs send -t. The first WRITE for the resume
#    object must be at the saved offset.
# 5. Finish the receive and compare the files.
# 6. At least one saved offset must fall inside a block.
#

verify_runnable "both"

typeset BLK=1048576
typeset sendfs=$POOL/rsl_fs
typeset recvfs=$POOL/rsl_recv
typeset full=$BACKDIR/rsl.full
typeset rest=$BACKDIR/rsl.rest

function cleanup
{
	rm -f $full $rest
	for ds in $sendfs $recvfs; do
		datasetexists $ds && destroy_dataset $ds -r
	done
}

function token_val # token name
{
	zstream token $1 | awk -v n="$2:" '$1 == n {print $2}'
}

function first_write_off # stream object
{
	zstream dump -v $1 | awk -v o=$2 '$1 == "WRITE" && $4 == o {
		for (i = 1; i < NF; i++)
			if ($i == "offset") {
				print $(i + 2)
				exit
			}
	}'
}

log_assert "A resumed send of split large blocks starts at the" \
    "saved offset"
log_onexit cleanup

log_must zfs create -o recordsize=1m $sendfs
typeset sendmnt=$(get_prop mountpoint $sendfs)
log_must file_write -o create -f $sendmnt/big -b $BLK -c 6 -d R
log_must file_write -o create -f $sendmnt/tail -b 300000 -c 1 -d R
log_must zfs snapshot $sendfs@snap
log_must eval "zfs send $sendfs@snap >$full"

typeset -i size=$(stat_size $full)
typeset -i inside=0
for cut in $((size * 2 / 9)) $((size * 4 / 9)) $((size * 6 / 9)) \
    $((size - 100000)) $((size - 15000)); do
	log_mustnot eval "head -c $cut $full | zfs recv -s $recvfs"
	typeset tok=$(get_prop receive_resume_token $recvfs)
	log_must test "$tok" != "-"
	typeset obj=$(token_val $tok object)
	typeset off=$(token_val $tok offset)
	log_must test -n "$off"
	log_note "cut $cut: obj $obj off $off"
	((off % BLK != 0)) && ((inside++))

	log_must eval "zfs send -t $tok >$rest"
	typeset first=$(first_write_off $rest $obj)
	log_must test "$first" = "$off"

	log_must eval "zfs recv -s $recvfs <$rest"
	typeset recvmnt=$(get_prop mountpoint $recvfs)
	log_must diff $sendmnt/big $recvmnt/big
	log_must diff $sendmnt/tail $recvmnt/tail
	log_must_busy zfs destroy -r $recvfs
done

log_note "$inside resume offsets fell inside a block"
log_must test $inside -ge 1

log_pass "A resumed send of split large blocks starts at the" \
    "saved offset"
