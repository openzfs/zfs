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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

#
# DESCRIPTION
# Verify that "zfs list" gives correct values for written and written@
# proerties for the dataset when different operations are on done on it
#
#
# STRATEGY
# 1) Create recursive datasets
# 2) Take snapshots, write data and verify  written/ written@ properties for
#    following cases
#    a) Delete data
#    b) Write Data
#    c) Clone
#    d) Delete snapshot
#    e) Recursive datasets

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

function cleanup
{
	for ds in $datasets; do
		destroy_dataset -R $TESTPOOL/$TESTFS1
	done
}
function get_prop_mb
{
	typeset prop=$1
	typeset dataset=$2
	typeset -l value=$(get_prop $prop $dataset)
	((value = value / mb_block))
	$ECHO $value
}

datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS2 \
    $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3"

log_assert "verify zfs written and written@ property"
log_onexit cleanup

typeset -l i=1
typeset -l blocks=50
typeset -l expected_written=0
typeset -l expected_writtenat=0
typeset -l written=0
typeset -l total=0
typeset -l snap1_size=0
typeset -l snap2_size=0
typeset -l snap3_size=0
typeset -l metadata=0
typeset -l mb_block=0
((mb_block = 1024 * 1024))
# approximate metadata on dataset when empty is 32KB
((metadata = 32 * 1024))

log_note "verify written property statistics for dataset"
log_must $ZFS create -p $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3
for i in 1 2 3; do
	log_must $ZFS snapshot $TESTPOOL/$TESTFS1@snap$i
	log_must $DD if=/dev/urandom of=/$TESTPOOL/$TESTFS1/testfile.$i bs=1M \
	    count=$blocks
	log_must $SYNC
	written=$(get_prop written $TESTPOOL/$TESTFS1)
	((expected_written=blocks * mb_block))
	within_percent $written $expected_written 99.5 || \
	    log_fail "Unexpected written value $written $expected_written"
	((total = total + blocks))
	((blocks = blocks + 50))
done

log_note "verify written property statistics for snapshots"
blocks=0
for i in 1 2 3; do
	written=$(get_prop written $TESTPOOL/$TESTFS1@snap$i)
	if [[ $blocks -eq 0 ]]; then
		expected_written=$metadata
	else
		((expected_written = blocks * mb_block))
	fi
	within_percent $written $expected_written 99.5 || \
	    log_fail "Unexpected written value $written $expected_written $i"
	((blocks = blocks + 50))
done

snap1_size=$total
((snap2_size = total-50))
((snap3_size = total-100))

log_note "verify written@ for the same dataset"
blocks=50
for i in 1 2 3; do
	writtenat=$(get_prop written@snap$i $TESTPOOL/$TESTFS1)
	((expected_writtenat = total * mb_block))
	within_percent $writtenat $expected_writtenat 99.5 || \
	    log_fail "Unexpected written@ value"
	((total = total - blocks))
	((blocks = blocks + 50))
done
log_note "delete data"
before_written=$(get_prop written $TESTPOOL/$TESTFS1)
log_must $RM /$TESTPOOL/$TESTFS1/testfile.3
snap3_size=0
log_must $SYNC
written=$(get_prop written $TESTPOOL/$TESTFS1)
writtenat3=$(get_prop written@snap3 $TESTPOOL/$TESTFS1)
[[ $written -eq $writtenat3 ]] || \
    log_fail "Written and written@ dont match $written $writtenat3"
within_percent $written $before_written 0.1 && \
    log_fail "Unexpected written value after delete $written $before_written"
writtenat=$(get_prop written@snap1 $TESTPOOL/$TESTFS1)
((snap1_size = snap1_size - 150))
((expected_writtenat = snap1_size * mb_block))
within_percent $writtenat $expected_writtenat 99.5 || \
    log_fail "Unexpected written value after delete $writtenat $expected_writtenat"
writtenat=$(get_prop written@snap2 $TESTPOOL/$TESTFS1)
((snap2_size = snap2_size - 150))
((expected_writtenat = snap2_size * mb_block))
within_percent $writtenat $expected_writtenat 99.5 || \
    log_fail "Unexpected written value after delete"

log_note "write data"
blocks=20
log_must $DD if=/dev/urandom of=/$TESTPOOL/$TESTFS1/testfile.3 bs=1M \
    count=$blocks
log_must $SYNC
written=$(get_prop written $TESTPOOL/$TESTFS1)
writtenat1=$(get_prop written@snap1 $TESTPOOL/$TESTFS1)
writtenat2=$(get_prop written@snap2 $TESTPOOL/$TESTFS1)
writtenat3=$(get_prop written@snap3 $TESTPOOL/$TESTFS1)
((snap3_size = snap3_size + blocks))
((expected_writtenat = snap3_size * mb_block))
[[ $written -eq $writtenat3 ]] || \
    log_fail "Unexpected_written value"
within_percent $writtenat3 $expected_writtenat 99.5 || \
    log_fail "Unexpected_written@ value for snap3"
((snap2_size = snap2_size + blocks))
((expected_writtenat = snap2_size * mb_block))
within_percent $writtenat2 $expected_writtenat 99.5 || \
    log_fail "Unexpected_written@ value for snap2"
((snap1_size = snap1_size + blocks))
((expected_writtenat = snap1_size * mb_block))
within_percent $writtenat1 $expected_writtenat 99.5 || \
    log_fail "Unexpected_written@ value for snap1"

log_note "write data to a clone"
before_clone=$(get_prop written $TESTPOOL/$TESTFS1)
log_must $ZFS clone $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS1/snap1.clone
log_must $DD if=/dev/urandom of=/$TESTPOOL/$TESTFS1/snap1.clone/testfile bs=1M \
    count=40
after_clone=$(get_prop written $TESTPOOL/$TESTFS1)
[[ $before_clone -eq $after_clone ]] || \
    log_fail "unexpected written for clone $before_clone $after_clone"

log_note "deleted snapshot"
typeset -l before_written1=$(get_prop_mb written@snap1 $TESTPOOL/$TESTFS1)
typeset -l before_written3=$(get_prop_mb written@snap3 $TESTPOOL/$TESTFS1)
typeset -l snap_before_written2=$(get_prop_mb written $TESTPOOL/$TESTFS1@snap2)
typeset -l snap_before_written3=$(get_prop_mb written $TESTPOOL/$TESTFS1@snap3)
destroy_dataset $TESTPOOL/$TESTFS1@snap2
log_mustnot snapexists $TESTPOOL/$TESTFS1@snap2
log_must $SYNC
written1=$(get_prop_mb written@snap1 $TESTPOOL/$TESTFS1)
written3=$(get_prop_mb written@snap3 $TESTPOOL/$TESTFS1)
[[ $before_written1 -eq $written1 && $before_written3 -eq $written3 ]] || \
    log_fail "unexpected written values $before_written1 $written1"
typeset -l expected_written3
((expected_written3 = snap_before_written2 + snap_before_written3))
prev_written=$(get_prop_mb written $TESTPOOL/$TESTFS1@snap3)
within_percent $prev_written $expected_written3 99.5 || \
    log_fail "unexpected written value $prev_written $expected_written3"

destroy_dataset $TESTPOOL/$TESTFS1@snap3
log_mustnot snapexists $TESTPOOL/$TESTFS1@snap3
written=$(get_prop written $TESTPOOL/$TESTFS1)
writtenat1=$(get_prop written@snap1 $TESTPOOL/$TESTFS1)
[[ $written -ne $writtenat1 ]] && \
    log_fail "Unexpected last snapshot written value"

log_note "verify written@ property for recursive datasets"
blocks=10
log_must $ZFS snapshot -r $TESTPOOL/$TESTFS1@now
for ds in $datasets; do
	writtenat=$(get_prop written@now $ds)
	[[ $writtenat -ne 0 ]] && \
	    log_fail "Unexpected written@ value"
	log_must $DD if=/dev/urandom of=/$ds/testfile bs=1M count=$blocks
	log_must $SYNC
	writtenat=$(get_prop written@now $ds)
	((expected_writtenat = blocks * mb_block))
	within_percent $writtenat $expected_writtenat 0.1 || \
	    log_fail "Unexpected written value"
	((blocks = blocks + 10))
done

log_note "verify written@ output for recursive datasets"
blocks=20
for ds in $datasets; do
	log_must $ZFS snapshot $ds@current
	log_must $DD if=/dev/urandom of=/$ds/testfile bs=1M \
	    count=$blocks
	log_must $SYNC
done
recursive_output=$($ZFS get -r written@current $TESTPOOL | \
    $GREP -v $TESTFS1@ | $GREP -v $TESTFS2@ | $GREP -v $TESTFS3@ | \
    $GREP -v "VALUE" | $GREP -v "-")
expected="20.0M"
for ds in $datasets; do
	writtenat=$($ECHO "$recursive_output" | $GREP -v $ds/)
	writtenat=$($ECHO "$writtenat" | $GREP $ds | $AWK '{print $3}')
	[[ $writtenat == $expected ]] || \
	    log_fail "recursive written property output mismatch"
done

log_pass "zfs written and written@ property fields print correct values"
