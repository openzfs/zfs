#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A block's parity is that of the layout it was born in, which is not
#	necessarily the vdev's.  Reconstruction that cannot rely on the
#	checksums of single columns must bound the failures it simulates, and
#	tell data columns from parity columns, by the parity of the block
#	being read, not by the parity of the vdev.
#
# STRATEGY:
#	1. Create a 5-wide raidz pool on file vdevs and give it a parity-epoch
#	   table that changes the parity at a txg that is yet to come.
#	2. Write one block past the epoch and silently corrupt some of its
#	   data columns, on every copy of the block.
#	3. Read it back in a new process, so that nothing comes from the ARC.
#	4. A block with a higher parity than the vdev is repaired from two
#	   silently corrupt columns.
#	5. A block with a lower parity than the vdev is repaired from one,
#	   and from two it fails with an error, without tripping an assertion.
#	6. The reads that never involved a mixed parity still behave.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_recon_mixed.$$
typeset -r POOL=${TESTPOOL}_mixed
typeset -i WIDTH=5
typeset -i ASHIFT=12

function cleanup
{
	rm -rf $BASEDIR
}

#
# recon_case <name> <vdev parity> <block parity> <block size> <ok|error>
#	<data column>...
#
function recon_case
{
	typeset name=$1 base=$2 par=$3 bs=$4 want=$5
	shift 5
	typeset dir=$BASEDIR/$name out txg offsets
	typeset -i epoch obj psize birth

	log_note "case $name: raidz$base vdev, parity $par block of $bs" \
	    "bytes, corrupt data columns: $*"
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH $base

	#
	# The epoch starts after every txg the pool has seen, so that the
	# block written below is the first one of it.
	#
	if (( base != par )); then
		log_must eval "zdb -lu $dir/v0 > $dir/labels.out"
		txg=$(awk '/txg = /{print $3}' $dir/labels.out |
		    sort -n | tail -1)
		[[ -n $txg ]] || log_fail "case $name: no txg in $dir/v0 labels"
		(( epoch = txg + 1 ))
		log_must zhack -d $dir raidz_epochs $POOL 0 \
		    0:$WIDTH:$base $epoch:$WIDTH:$par
	fi

	out=$(raidz_epoch_test write $dir $POOL $bs) ||
	    log_fail "write failed: $out"
	log_note "$out"
	obj=$(print -r -- "$out" | sed -n 's/.* object=\([0-9]*\).*/\1/p')
	psize=$(print -r -- "$out" | sed -n 's/.* psize=\([0-9]*\).*/\1/p')
	birth=$(print -r -- "$out" | sed -n 's/.* birth=\([0-9]*\).*/\1/p')
	offsets=$(print -r -- "$out" |
	    sed -n 's/.* offsets=\([0-9,]*\).*/\1/p')
	if (( base != par )) && (( birth < epoch )); then
		log_fail "block born in txg $birth, before the epoch at $epoch"
	fi

	log_must raidz_epoch_test corrupt $dir $offsets $psize $WIDTH $par \
	    $ASHIFT "$@"

	#
	# The helper exits 0 when the read ran to completion, so an assertion
	# failing in the middle of it is a failure here, not an error.
	#
	out=$(raidz_epoch_test read $dir $POOL $obj $bs) ||
	    log_fail "read did not complete: $out"
	log_note "$out"
	if [[ $want == ok ]]; then
		[[ $out == *"rc=0 match=1"* ]] ||
		    log_fail "case $name: read did not return the data"
	else
		[[ $out == *"rc=0 "* ]] &&
		    log_fail "case $name: read of an unrecoverable block" \
		    "succeeded"
		[[ $out == *"match=0"* ]] ||
		    log_fail "case $name: unexpected result"
	fi
}

log_onexit cleanup
log_assert "reconstruction bounds the failures by the parity of the block"

# A block with a higher parity than the vdev: two silent errors are in reach.
recon_case up2 1 2 8192 ok 0 1

# A block with a lower parity than the vdev: one silent error is in reach,
# two are not, and neither may trip an assertion.
recon_case down1 2 1 16384 ok 0
recon_case down2 2 1 16384 error 0 1

# The vdev's own parity, without an epoch table.
recon_case same2 2 2 8192 ok 0 1
recon_case plain1 1 1 8192 ok 0

log_pass "reconstruction bounded the failures by the parity of the block"
