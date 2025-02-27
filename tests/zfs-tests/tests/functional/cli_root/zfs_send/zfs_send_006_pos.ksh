#!/bin/ksh
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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, 2018 by Delphix. All rights reserved.
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
	log_must set_tunable32 OVERRIDE_ESTIMATE_RECORDSIZE 8192
	for ds in $datasets; do
                destroy_dataset $ds "-rf"
	done
}

function cal_percentage
{
	typeset value=$1
	return=$(echo "$PERCENT * $value" | bc)
	return=$(echo "$return / 100" | bc)
	echo $return
}

function get_estimate_size
{
	typeset snapshot=$1
	typeset option=$2
	typeset base_snapshot=${3:-""}
	if [[ -z $3 ]]; then
		typeset total_size=$(zfs send $option $snapshot 2>&1 | tail -1)
	else
		typeset total_size=$(zfs send $option $base_snapshot $snapshot \
		     2>&1 | tail -1)
	fi
	total_size=$(echo "$total_size" | awk '{print $NF}')
	if [[ $options != *"P"* ]]; then
		total_size=${total_size%M}
		total_size=$(echo "$total_size * $block_count" | bc)
	fi
	echo $total_size

}

function verify_size_estimates
{
	typeset options=$1
	typeset file_size=$2
	typeset refer_diff=$(echo "$refer_size - $estimate_size" | bc)
	refer_diff=$(echo "$refer_diff / 1" | bc)
	refer_diff=$(echo "$refer_diff" | awk '{print ($1 < 0) ? ($1 * -1): $1'})
	typeset file_diff=$(echo "$file_size - $estimate_size" | bc)
	file_diff=$(echo "$file_diff / 1" | bc)
	file_diff=$(echo "$file_diff" | awk '{print ($1 < 0) ? ($1 * -1):$1'})
	typeset expected_diff=$(cal_percentage $refer_size)

	[[ -z $refer_diff && -z $file_diff && -z $expected_diff ]] && \
	    log_fail "zfs send $options failed"
	[[ $refer_diff -le $expected_diff &&  \
	    $file_diff -le $expected_diff ]] || \
	    log_fail "zfs send $options gives wrong size estimates"
}

log_assert "Verify 'zfs send -nvP' generates valid stream estimates"
log_onexit cleanup
log_must set_tunable32 OVERRIDE_ESTIMATE_RECORDSIZE 0
typeset -l block_count=0
typeset -l block_size
typeset -i PERCENT=1

((block_count=1024*1024))

# create dataset
log_must zfs create $TESTPOOL/$TESTFS1

# create multiple snapshot for the dataset with data
for block_size in 64 128 256; do
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS1/file$block_size \
	    bs=1M count=$block_size
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$block_size
	log_must zfs bookmark $TESTPOOL/$TESTFS1@snap$block_size \
	    "$TESTPOOL/$TESTFS1#bmark$block_size"
done

full_snapshot="$TESTPOOL/$TESTFS1@snap64"
incremental_snapshot="$TESTPOOL/$TESTFS1@snap256"
full_bookmark="$TESTPOOL/$TESTFS1#bmark64"
incremental_bookmark="$TESTPOOL/$TESTFS1#bmark256"

full_size=$(zfs send $full_snapshot 2>&1 | wc -c)
incremental_size=$(zfs send $incremental_snapshot 2>&1 | wc -c)
incremental_send=$(zfs send -i $full_snapshot $incremental_snapshot 2>&1 | wc -c)

log_note "verify zfs send -nvV"
options="-nvV"
refer_size=$(get_prop refer $full_snapshot)
estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $full_size

log_note "verify zfs send -PnvV"
options="-PnvV"

estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $full_size

log_note "verify zfs send -nvV for multiple snapshot send"
options="-nvV"
refer_size=$(get_prop refer $incremental_snapshot)

estimate_size=$(get_estimate_size $incremental_snapshot $options)
log_must verify_size_estimates $options $incremental_size

log_note "verify zfs send -vVPn for multiple snapshot send"
options="-vVPn"

estimate_size=$(get_estimate_size $incremental_snapshot $options)
log_must verify_size_estimates $options $incremental_size

log_note "verify zfs send -invV for incremental send"
options="-nvVi"
refer_size=$(get_prop refer $incremental_snapshot)
deduct_size=$(get_prop refer $full_snapshot)
refer_size=$(echo "$refer_size - $deduct_size" | bc)

estimate_size=$(get_estimate_size $incremental_snapshot $options $full_snapshot)
log_must verify_size_estimates $options $incremental_send
estimate_size=$(get_estimate_size $incremental_snapshot $options $full_bookmark)
log_must verify_size_estimates $options $incremental_send

log_note "verify zfs send -ivVPn for incremental send"
options="-vVPni"

estimate_size=$(get_estimate_size $incremental_snapshot $options $full_snapshot)
log_must verify_size_estimates $options $incremental_send
estimate_size=$(get_estimate_size $incremental_snapshot $options $full_bookmark)
log_must verify_size_estimates $options $incremental_send

log_must zfs destroy -r $TESTPOOL/$TESTFS1

#setup_recursive_send
datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS2
    $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3"
# create nested datasets
log_must zfs create -p $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3

# verify dataset creation
for ds in $datasets; do
        datasetexists $ds || log_fail "Create $ds dataset fail."
done
for ds in $datasets; do
	log_must dd if=/dev/urandom of=/$ds/file64 \
	    bs=1M count=64
done

# create recursive nested snapshot
log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap64
for ds in $datasets; do
        datasetexists $ds@snap64 || log_fail "Create $ds@snap64 snapshot fail."
done
recursive_size=$(zfs send -R $full_snapshot 2>&1 | wc -c)
log_note "verify zfs send -RnvV for recursive send"
options="-RnvV"
refer_size=$(get_prop refer $full_snapshot)
refer_size=$(echo "$refer_size * 3" | bc)

estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $recursive_size

log_note "verify zfs send -RvVPn for recursive send"
options="-RvVPn"
estimate_size=$(get_estimate_size $full_snapshot $options)
log_must verify_size_estimates $options $recursive_size

log_pass "'zfs send' prints the correct size estimates using '-n' and '-P' options."
