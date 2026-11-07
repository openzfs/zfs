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
# zfs send -w -t sends a raw stream even when the resume token came
# from a send that was not raw.
#
# Strategy:
# 1. Cut a plain send of an encrypted dataset short and save the
#    resume token of the receive.
# 2. Resume with zfs send -t. The stream must parse and not be raw.
# 3. Resume with zfs send -w -t. The stream must be raw.
#

verify_runnable "both"

typeset passphrase="password1"
typeset src=$POOL/rrt_src
typeset dst=$POOL2/rrt_dst
typeset stream=$BACKDIR/rrt.zs
typeset dump=$BACKDIR/rrt.dump

function cleanup
{
	for ds in $src $dst; do
		datasetexists $ds && destroy_dataset $ds -r
	done
	rm -f $BACKDIR/rrt.*
}

function drr_records # dump type
{
	awk -v t=DRR_$2 '$1 == "Total" && $2 == t {print $5}' $1
}

log_assert "zfs send -w -t sends a raw stream"
log_onexit cleanup

log_must eval "echo $passphrase | zfs create -o encryption=on" \
    "-o keyformat=passphrase $src"
typeset mntpnt=$(get_prop mountpoint $src)
log_must file_write -o create -f $mntpnt/f1 -b 131072 -c 64 -d R
log_must zfs snapshot $src@a
log_must eval "zfs send $src@a >$stream"
typeset -i half=$(stat_size $stream)/2
log_mustnot eval "head -c $half $stream | zfs recv -s -u $dst"
typeset tok=$(get_prop receive_resume_token $dst)
log_must test -n "$tok"
log_must test "$tok" != "-"

log_must eval "zfs send -t $tok >$BACKDIR/rrt.plain"
log_must eval "zstream dump $BACKDIR/rrt.plain >$dump"
typeset -i n=$(drr_records $dump BEGIN)
log_must test $n -eq 1
n=$(drr_records $dump WRITE)
log_note "WRITE records without -w: $n"
log_must test $n -gt 0
n=$(drr_records $dump OBJECT_RANGE)
log_note "OBJECT_RANGE records without -w: $n"
log_must test $n -eq 0

log_must eval "zfs send -w -t $tok >$BACKDIR/rrt.raw"
log_must eval "zstream dump $BACKDIR/rrt.raw >$dump"
n=$(drr_records $dump OBJECT_RANGE)
log_note "OBJECT_RANGE records with -w: $n"
log_must test $n -gt 0

log_pass "zfs send -w -t sends a raw stream"
