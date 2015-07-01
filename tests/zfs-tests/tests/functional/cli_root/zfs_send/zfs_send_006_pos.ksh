#!/bin/ksh
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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
#
# DESCRIPTION:
#	Verify 'zfs send' can generate valid streams with different options
#
# STRATEGY:
#	1. Create datasets
#	2. Write some data to the datasets
#	3. Create a full send streams
#	4. Receive the send stream
#	5. Do a dry run with different options and verify the generated size
#          estimate against the received stream
#

verify_runnable "both"

function cleanup
{
	for ds in $datasets; do
		destroy_dataset -rf $ds
	done
}

function cal_percentage
{
	typeset value=$1
	return=$($ECHO "$PERCENT * $value" | bc)
	return=$($ECHO "$return / 100" | bc)
	echo $return
}

function get_estimate_size
{
	typeset snapshot=$1
	typeset option=$2
	typeset base_snapshot=${3:-""}
	if [[ -z $3 ]];then
		typeset total_size=$($ZFS send $option $snapshot 2>&1 | $TAIL -1)
	else
		typeset total_size=$($ZFS send $option $base_snapshot $snapshot \
		     2>&1 | $TAIL -1)
	fi
	if [[ $options == *"P"* ]]; then
		total_size=$($ECHO "$total_size" | $AWK '{print $2}')
	else
		total_size=$($ECHO "$total_size" | $AWK '{print $5}')
		total_size=${total_size%M}
		total_size=$($ECHO "$total_size * $block_count" | bc)
	fi
	$ECHO $total_size

}

function verify_size_estimates
{
	typeset options=$1
	typeset file_size=$2
	typeset refer_diff=$($ECHO "$refer_size - $estimate_size" | bc)
	refer_diff=$($ECHO "$refer_diff / 1" | bc)
	refer_diff=$($ECHO "$refer_diff" | $NAWK '{print ($1 < 0) ? ($1 * -1): $1'})
	typeset file_diff=$($ECHO "$file_size - $estimate_size" | bc)
	file_diff=$($ECHO "$file_diff / 1" | bc)
	file_diff=$($ECHO "$file_diff" | $NAWK '{print ($1 < 0) ? ($1 * -1):$1'})
	typeset expected_diff=$(cal_percentage $refer_size)

	[[ -z $refer_diff && -z $file_diff && -z $expected_diff ]] && \
	    log_fail "zfs send $options failed"
	[[ $refer_diff -le $expected_diff &&  \
	    $file_diff -le $expected_diff ]] || \
	    log_fail "zfs send $options gives wrong size estimates"
}

log_assert "Verify 'zfs send -nvP' generates valid stream estimates"
log_onexit cleanup
typeset -l block_count=0
typeset -l block_size
typeset -i PERCENT=1

((block_count=1024*1024))

# create dataset
log_must $ZFS create $TESTPOOL/$TESTFS1

# create multiple snapshot for the dataset with data
for block_size in 64 128 256; do
	log_must $DD if=/dev/urandom of=/$TESTPOOL/$TESTFS1/file$block_size \
	    bs=1M count=$block_size
	log_must $ZFS snapshot $TESTPOOL/$TESTFS1@snap$block_size
done

full_snapshot="$TESTPOOL/$TESTFS1@snap64"
increamental_snapshot="$TESTPOOL/$TESTFS1@snap256"

full_size=$($ZFS send $full_snapshot 2>&1 | wc -c)
increamental_size=$($ZFS send $increamental_snapshot 2>&1 | wc -c)
increamental_send=$($ZFS send -i $full_snapshot $increamental_snapshot 2>&1 | wc -c)

log_note "verify zfs send -nv"
options="-nv"
refer_size=$(get_prop refer $full_snapshot)
estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $full_size

log_note "verify zfs send -Pnv"
options="-Pnv"

estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $full_size

log_note "verify zfs send -nv for multiple snapshot send"
options="-nv"
refer_size=$(get_prop refer $increamental_snapshot)

estimate_size=$(get_estimate_size $increamental_snapshot $options)
log_must verify_size_estimates $options $increamental_size

log_note "verify zfs send -vPn for multiple snapshot send"
options="-vPn"

estimate_size=$(get_estimate_size $increamental_snapshot $options)
log_must verify_size_estimates $options $increamental_size

log_note "verify zfs send -inv for increamental send"
options="-nvi"
refer_size=$(get_prop refer $increamental_snapshot)
deduct_size=$(get_prop refer $full_snapshot)
refer_size=$($ECHO "$refer_size - $deduct_size" | bc)

estimate_size=$(get_estimate_size $increamental_snapshot $options $full_snapshot)
log_must verify_size_estimates $options $increamental_send

log_note "verify zfs send -ivPn for increamental send"
options="-vPni"

estimate_size=$(get_estimate_size $increamental_snapshot $options $full_snapshot)
log_must verify_size_estimates $options $increamental_send

destroy_dataset -r $TESTPOOL/$TESTFS1

#setup_recursive_send
datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS2
    $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3"
# create nested datasets
log_must $ZFS create -p $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3

# verify dataset creation
for ds in $datasets; do
        datasetexists $ds || log_fail "Create $ds dataset fail."
done
for ds in $datasets; do
	log_must $DD if=/dev/urandom of=/$ds/file64 \
	    bs=1M count=64
done

# create recursive nested snapshot
log_must $ZFS snapshot -r $TESTPOOL/$TESTFS1@snap64
for ds in $datasets; do
        datasetexists $ds@snap64 || log_fail "Create $ds@snap64 snapshot fail."
done
recursive_size=$($ZFS send -R $full_snapshot 2>&1 | wc -c)
log_note "verify zfs send -Rnv for recursive send"
options="-Rnv"
refer_size=$(get_prop refer $full_snapshot)
refer_size=$($ECHO "$refer_size * 3" | bc)

estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $recursive_size

log_note "verify zfs send -RvPn for recursive send"
options="-RvPn"
estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $recursive_size

log_pass "'zfs send' prints the correct size estimates using '-n' and '-P' options."
