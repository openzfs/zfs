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
#	The copy of a parity-epoch table in the vdev ZAP is the authoritative
#	one; the copy in the labels and the MOS config is what the MOS is
#	read with until the ZAP is reached.  A table that the config carries
#	and the ZAP does not was not stored by the setter: the import drops
#	it, says so, says so again if the pool has reached an epoch of it,
#	whose blocks cannot be read without it, and the config is written
#	back without it.  A table that only the ZAP has is used, said to be,
#	and written back to the config.
#
# STRATEGY:
#	1. Create a raidz1 pool and give its vdev a table in the config only,
#	   whose new epoch starts at a txg the pool never reaches.
#	2. Require zdb to open the pool and say that it drops the table, and
#	   nothing about an epoch the pool has reached.
#	3. Write a block in a read-write import, and require the labels and
#	   the MOS config written back to carry no table.
#	4. On a second pool, store a table in the vdev ZAP only, with
#	   zhack -f: require zdb to say that it uses the table that the config
#	   lacks, and a read-write import to write it back to the labels and
#	   the MOS config.
#	5. On a third pool, give the vdev a table in the config only whose
#	   new epoch starts at the first txg the setter would allow, and write
#	   a block in that epoch: require zdb to name the epoch when it drops
#	   the table.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_label.$$
typeset -r POOL=${TESTPOOL}_label
typeset -r FAR=1000000
typeset -r MSG_DROP="no parity epoch table in the vdev ZAP, dropping"
typeset -r MSG_REACHED="the dropped table has an epoch of parity"
typeset -r MSG_ZAP="no parity epoch table in the config, using"
typeset -i WIDTH=5
typeset -i n
typeset dir out start birth

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

log_onexit cleanup
log_assert "the copy of a parity-epoch table in the vdev ZAP is the one used"

# A table in the config only, never reached: dropped, and written back so.
dir=$BASEDIR/config
log_must mkdir -p $dir
log_must raidz_epoch_test create $dir $POOL $WIDTH 1
log_must raidz_epoch_test label $dir $POOL
count_values $dir label
(( n >= 6 )) || log_fail "the labels do not carry the table"
log_must eval "zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1"
log_must grep -q "$MSG_DROP" $dir/zdb.out
log_mustnot grep -q "$MSG_REACHED" $dir/zdb.out
out=$(raidz_epoch_test write $dir $POOL 8192) || log_fail "write failed: $out"
log_note "$out"
count_values $dir label
(( n == 0 )) || log_fail "the labels written back carry the dropped table"
count_values $dir config
(( n == 0 )) || log_fail "the MOS config written back carries the table"

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

# A table in the config only, with blocks in its new epoch.
dir=$BASEDIR/reached
log_must mkdir -p $dir
log_must raidz_epoch_test create $dir $POOL $WIDTH 1
out=$(raidz_epoch_test label $dir $POOL now) || log_fail "label failed: $out"
log_note "$out"
start=$(print -r -- "$out" |
    sed -n 's/^RESULT label rc=0 start=\([0-9]*\) birth=.*/\1/p')
birth=$(print -r -- "$out" | sed -n 's/^RESULT label rc=0 .* birth=//p')
[[ $start == +([0-9]) && $birth == +([0-9]) ]] ||
    log_fail "no start or birth txg in: $out"
(( birth >= start )) ||
    log_fail "the block was born in txg $birth, before the epoch at $start"
zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1
log_note "zdb -m exited $?"
log_must grep -q "$MSG_DROP" $dir/zdb.out
log_must grep -q "$MSG_REACHED 2 from txg $start," $dir/zdb.out

log_pass "the copy of a parity-epoch table in the vdev ZAP is the one used"
