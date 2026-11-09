#!/bin/ksh
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
# 'zfs send --saved' works on a partially received filesystem whose
# stream was cut in its first few records, including after the ZPL
# master node object was created but before its contents arrived.
#
# Strategy:
# 1. Make a full stream of a small filesystem, and a raw ('zfs send
#    -w') full stream of a small encrypted filesystem.
# 2. For each stream and a series of cut points through its first
#    records (the master node's OBJECT, FREE and WRITE records):
#    a. Receive the cut stream with 'zfs recv -s' (fails, leaving a
#       partially received filesystem).
#    b. 'zfs send --saved' it: this must succeed with no error output
#       and produce one DRR_BEGIN record for the original snapshot,
#       with the full stream's feature flags, and no DRR_END record.
#    c. Receive the saved stream with 'zfs recv -s' (fails, leaving a
#       second partial filesystem whose token names the original
#       snapshot), resume it from the source, and compare the data.
#    d. Destroy both partial filesystems (nothing left held).
#

verify_runnable "both"

log_assert "'zfs send --saved' works on a stream cut in its first records"

plainfs=$POOL/plainfs
cryptfs=$POOL/cryptfs
cutfs=$POOL/cutfs
savedfs=$POOL/savedfs
streams=$(mktemp -d $TEST_BASE_DIR/send_saved_early_cut.XXXXXX)
keyfile=$streams/key
typeset -i saved_failed=0

function cleanup
{
	destroy_dataset $cutfs "-r"
	destroy_dataset $savedfs "-r"
	destroy_dataset $plainfs "-r"
	destroy_dataset $cryptfs "-r"
	rm -f $streams/full.send $streams/cut.send $streams/saved.send \
	    $streams/saved.err $streams/dump $keyfile
	rmdir $streams
}
log_onexit cleanup

#
# Cut the full stream of SNAP (made with 'zfs send FLAGS') at each
# point and check the saved send of the partial receive.
#
function cut_and_forward # snap flags
{
	typeset snap=$1
	typeset flags=$2
	typeset fs=${snap%@*}
	typeset -i off=624 checked=0
	typeset tok toguid features

	log_must eval "zfs send $flags $snap > $streams/full.send"
	toguid=0x$(zstream dump $streams/full.send | \
	    awk '/toguid =/ {print $3; exit}')
	features=$(zstream dump $streams/full.send | \
	    awk '/features =/ {print $3; exit}')

	#
	# Each record header is 312 bytes. The stream starts with
	# DRR_BEGIN (and, for a raw stream, its payload), then the master
	# node (object 1): DRR_OBJECT, DRR_FREE and a DRR_WRITE with its
	# contents. Cut every 104 bytes from the end of the first record
	# after DRR_BEGIN (shorter streams create no dataset) to well
	# past the master node's DRR_WRITE.
	#
	while (( off <= 4160 )); do
		log_must eval "head -c $off $streams/full.send > \
		    $streams/cut.send"
		log_mustnot eval "zfs recv -s $cutfs < $streams/cut.send"
		if ! datasetexists $cutfs; then
			# a raw stream's DRR_BEGIN has a longer payload
			off=$((off + 104))
			continue
		fi
		tok=$(get_prop receive_resume_token $cutfs)
		[[ -n $tok && $tok != "-" ]] || \
		    log_fail "no resume token on $cutfs (cut at $off)"
		checked=$((checked + 1))

		# The saved send itself must succeed, with no error output.
		if ! zfs send --saved $cutfs > $streams/saved.send \
		    2> $streams/saved.err || [[ -s $streams/saved.err ]]; then
			log_note "$snap ${flags:-(plain)} cut at $off:" \
			    "zfs send --saved failed: $(cat $streams/saved.err)"
			saved_failed=$((saved_failed + 1))
			log_must zfs destroy -r $cutfs
			off=$((off + 104))
			continue
		fi

		log_must eval "zstream dump $streams/saved.send > $streams/dump"
		log_must grep -q "Total DRR_BEGIN records = 1 " $streams/dump
		log_must grep -q "Total DRR_END records = 0 " $streams/dump
		log_must grep -q "toname = $snap\$" $streams/dump
		log_must grep -q "features = $features\$" $streams/dump

		log_mustnot eval "zfs recv -s $savedfs < $streams/saved.send"
		tok=$(get_prop receive_resume_token $savedfs)
		[[ -n $tok && $tok != "-" ]] || \
		    log_fail "no resume token on $savedfs (cut at $off)"
		log_must eval "zfs send -nv -t $tok 2>&1 | \
		    grep -q 'toguid = $toguid\$'"
		log_must eval "zfs send -nv -t $tok 2>&1 | \
		    grep -q 'toname = $snap\$'"
		log_must eval "zfs send -t $tok | zfs recv -s $savedfs"
		if [[ $fs == $cryptfs ]]; then
			log_must eval "zfs load-key -L file://$keyfile $savedfs"
		fi
		file_check $fs $savedfs

		log_must zfs destroy -r $savedfs
		log_must zfs destroy -r $cutfs
		off=$((off + 104))
	done
	(( checked >= 10 )) || \
	    log_fail "only $checked cut points of $snap left a partial receive"
}

log_must zfs create -o compression=off $plainfs
log_must mkfile 256k /$plainfs/filea
log_must zfs snap $plainfs@a
cut_and_forward $plainfs@a ""

log_must eval "echo 'password' > $keyfile"
log_must zfs create -o compression=off -o encryption=on \
    -o keyformat=passphrase -o keylocation=file://$keyfile $cryptfs
log_must mkfile 256k /$cryptfs/filea
log_must zfs snap $cryptfs@a
cut_and_forward $cryptfs@a "-w"

(( saved_failed == 0 )) || \
    log_fail "'zfs send --saved' failed for $saved_failed cut point(s)"

log_pass "'zfs send --saved' works on a stream cut in its first records"
