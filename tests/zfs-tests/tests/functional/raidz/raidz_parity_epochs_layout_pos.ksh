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
#	zdb -f shows how the blocks of an object are laid out across the
#	children of a raidz vdev.  A block is laid out with the parity of the
#	epoch it was born in, so the layout shown must have that many parity
#	columns, whatever the parity of the vdev.
#
# STRATEGY:
#	1. Create a raidz1 pool on file vdevs and write a block.
#	2. Raise the parity to 2 with zhack from the first txg it allows, and
#	   write another block.
#	3. Require zdb -f to show one parity column for the first block and
#	   two for the second.
#	4. The same with a raidz2 pool whose parity is lowered to 1.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_layout.$$
typeset -r POOL=${TESTPOOL}_layout
typeset -i WIDTH=5
typeset out
typeset -i obj birth

function cleanup
{
	rm -rf $BASEDIR
}

# Write a 128k block to a new object in <dir>; set obj and birth.
function put_block
{
	typeset dir=$1 v

	out=$(raidz_epoch_test put $dir $POOL 131072) ||
	    log_fail "put failed: $out"
	log_note "$out"
	v=$(print -r -- "$out" | sed -n 's/.* object=\([0-9]*\).*/\1/p')
	[[ $v == +([0-9]) ]] || log_fail "no object in: $out"
	obj=$v
	v=$(print -r -- "$out" | sed -n 's/.* birth=\([0-9]*\).*/\1/p')
	[[ $v == +([0-9]) ]] || log_fail "no birth txg in: $out"
	birth=$v
}

# check_layout <dir> <object> <parity>: zdb -f shows that many parity columns.
function check_layout
{
	typeset dir=$1 object=$2 want=$3
	typeset -i n

	log_must eval "zdb -e -p $dir -f -H -v $POOL/ $object > $dir/layout.out"
	n=$(awk -F'\t' '$NF ~ /^P[0-9]+$/ {n++} END {print n + 0}' \
	    $dir/layout.out)
	(( n == want )) || log_fail "object $object: zdb shows $n parity" \
	    "columns, the epoch its block was born in has parity $want"
}

# layout_case <name> <vdev parity> <parity of the new epoch>
function layout_case
{
	typeset name=$1 base=$2 par=$3
	typeset dir=$BASEDIR/$name last
	typeset -i start before after

	log_note "case $name: raidz$base vdev, new epoch of parity $par"
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH $base
	put_block $dir
	(( before = obj ))

	out=$(zhack -d $dir raidz_epochs $POOL 0 \
	    0:$WIDTH:$base next:$WIDTH:$par) ||
	    log_fail "case $name: zhack failed: $out"
	log_note "$out"
	last=${out##* }
	[[ ${last%%:*} == +([0-9]) ]] || log_fail "no epoch start in: $out"
	start=${last%%:*}
	(( birth < start )) || log_fail "case $name: the first block was" \
	    "born in txg $birth, not before the epoch at $start"

	put_block $dir
	(( after = obj ))
	(( birth >= start )) || log_fail "case $name: the second block was" \
	    "born in txg $birth, before the epoch at $start"

	check_layout $dir $before $base
	check_layout $dir $after $par
}

log_onexit cleanup
log_assert "zdb -f shows a block with the parity of its epoch"

layout_case up 1 2
layout_case down 2 1

log_pass "zdb -f shows a block with the parity of its epoch"
