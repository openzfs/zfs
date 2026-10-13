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
#	A config-only parity-epoch table may describe live blocks.  Preserve
#	it for read-only recovery and refuse a writable import.  The refusal
#	and recovery reads must leave the labels and MOS config unchanged.
#	A table in the vdev ZAP only remains authoritative and is propagated
#	to the config on a writable import.
#
# STRATEGY:
#	1. Create config-only tables with future and reached parity epochs.
#	2. Refuse a writable import and compare all vdev bytes before/after.
#	3. Read the known block through a read-only import, and compare all
#	   vdev bytes again.  Verify that labels and MOS config keep the table.
#	4. Store a ZAP-only table and verify its normal import and propagation.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_label.$$
typeset -r POOL=${TESTPOOL}_label
typeset -r FAR=1000000
typeset -r MSG_KEEP="preserving the config table for read-only recovery"
typeset -r MSG_ZAP="no parity epoch table in the config, using"
typeset -i WIDTH=5
typeset -i n
typeset dir out start birth obj

function cleanup
{
	rm -rf $BASEDIR
}

# count_values <dir> <label|config>: set n to the values of the table there.
function count_values
{
	typeset d=$1 where=$2

	if [[ $where == label ]]; then
		log_must eval "zdb -l $d/v0 > $d/where.out"
	else
		log_must eval "zdb -e -p $d -C $POOL > $d/where.out"
	fi
	n=$(grep -c 'raidz_parity_epochs\[' $d/where.out)
}

# config_case <future|reached>: preserve the table and the known block.
function config_case
{
	typeset name=$1
	dir=$BASEDIR/$name
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH 1
	if [[ $name == reached ]]; then
		out=$(raidz_epoch_test label $dir $POOL now) ||
		    log_fail "label failed: $out"
		log_note "$out"
		start=$(print -r -- "$out" |
		    sed -n 's/.* start=\([0-9]*\).*/\1/p')
		birth=$(print -r -- "$out" |
		    sed -n 's/.* birth=\([0-9]*\).*/\1/p')
		[[ $start == +([0-9]) && $birth == +([0-9]) ]] ||
		    log_fail "no start or birth txg in: $out"
		(( birth >= start )) || log_fail "epoch was not reached: $out"
	else
		out=$(raidz_epoch_test write $dir $POOL 8192) ||
		    log_fail "write failed: $out"
		log_must raidz_epoch_test label $dir $POOL
	fi
	obj=$(print -r -- "$out" |
	    sed -n 's/.* object=\([0-9]*\).*/\1/p')
	[[ $obj == +([0-9]) ]] || log_fail "no object in: $out"

	count_values $dir label
	(( n >= 6 )) || log_fail "the labels do not carry the table"
	log_must eval "cksum $dir/v* > $dir/before.cksum"
	log_mustnot raidz_epoch_test write $dir $POOL 8192
	log_must eval "cksum $dir/v* > $dir/after.cksum"
	log_must cmp $dir/before.cksum $dir/after.cksum
	out=$(raidz_epoch_test read $dir $POOL $obj 8192 readonly) ||
	    log_fail "read-only import failed: $out"
	log_note "$out"
	[[ $out == *"rc=0 match=1"* ]] || log_fail "read mismatch: $out"
	log_must eval "zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1"
	log_must grep -q "$MSG_KEEP" $dir/zdb.out
	count_values $dir label
	(( n >= 6 )) || log_fail "the labels lost the recovery table"
	count_values $dir config
	(( n == 6 )) || log_fail "the MOS config lost the recovery table"
	log_must eval "cksum $dir/v* > $dir/after.cksum"
	log_must cmp $dir/before.cksum $dir/after.cksum
}

log_onexit cleanup
log_assert "config-only epochs preserve data through read-only recovery"
config_case future
config_case reached

# A table in the vdev ZAP only: used, and written back to the config.
dir=$BASEDIR/zap
log_must mkdir -p $dir
log_must raidz_epoch_test create $dir $POOL $WIDTH 1
log_must zhack -d $dir raidz_epochs -f $POOL 0 0:$WIDTH:1 $FAR:$WIDTH:2
count_values $dir label
(( n == 0 )) || log_fail "zhack -f wrote the table to the labels"
log_must eval "zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1"
log_must grep -q "$MSG_ZAP the 2 entries of the vdev ZAP's" $dir/zdb.out
log_must grep -q "raidz_parity_epochs 2: \[txg>=0 w=$WIDTH p=1\]" \
    $dir/zdb.out
out=$(raidz_epoch_test write $dir $POOL 8192) || log_fail "write failed: $out"
log_note "$out"
count_values $dir label
(( n >= 6 )) || log_fail "the labels written back lack the table"
count_values $dir config
(( n == 6 )) || log_fail "the MOS config written back lacks the table"

log_pass "config-only epochs preserve data; ZAP epochs propagate normally"
