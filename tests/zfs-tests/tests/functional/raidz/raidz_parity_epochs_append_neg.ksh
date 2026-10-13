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
#	A parity-epoch table only grows, and only into txgs that cannot hold
#	a block yet.  Blocks are laid out by the epoch of their physical birth
#	txg and nothing is rewritten, so an epoch that started in a txg that
#	was already written, as the txgs zhack's own import writes before it
#	stores the table are, would leave blocks of the pool unreadable.
#
# STRATEGY:
#	1. Create a raidz1 pool on file vdevs and note its newest txg.
#	2. Require zhack to refuse an epoch that starts right after it, say
#	   which start was the first allowed in that run, and store nothing.
#	3. Store the epoch with a start of "next": zhack prints the start it
#	   chose, past every txg the pool has written.
#	4. Require zdb to read every block of the pool, and a block written
#	   afterwards to be born in the new epoch.
#	5. Require zhack to refuse tables that drop or change that epoch, and
#	   to store one that appends another epoch after it.
#	6. On a second pool, in one sync task, require the check of the
#	   setter and the setter to refuse an epoch that starts at the txg of
#	   the task or one or two txgs after it, and to store one that starts
#	   three txgs after it, which zdb reads back whole.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_append.$$
typeset -r POOL=${TESTPOOL}_append
typeset -i WIDTH=5
typeset -i txg start birth
typeset out table last task

function cleanup
{
	rm -rf $BASEDIR
}

# Set txg to the newest txg in the labels of the first child.
function get_txg
{
	log_must eval "zdb -lu $BASEDIR/v0 > $BASEDIR/labels.out"
	txg=$(awk '/txg = /{print $3}' $BASEDIR/labels.out | sort -n | tail -1)
	(( txg > 0 )) || log_fail "no txg in the labels of $BASEDIR/v0"
}

# Set table to the table of vdev 0 that zdb prints, as zhack takes it.
function get_table
{
	log_must eval "zdb -e -p $BASEDIR -m $POOL > $BASEDIR/zdb.out"
	table=$(sed -n 's/.*raidz_parity_epochs [0-9]*: //p' $BASEDIR/zdb.out |
	    sed 's/\[txg>=\([0-9]*\) w=\([0-9]*\) p=\([0-9]*\)\]/\1:\2:\3/g')
}

# Every block of the pool can be read and is accounted for.
function check_blocks
{
	log_must eval "zdb -e -p $BASEDIR -bcc $POOL > $BASEDIR/zdb.out 2>&1"
	log_must grep -q "No leaks" $BASEDIR/zdb.out
}

log_onexit cleanup
log_assert "a parity-epoch table only grows, past the txgs already written"

log_must mkdir -p $BASEDIR
log_must raidz_epoch_test create $BASEDIR $POOL $WIDTH 1

get_txg
log_mustnot_expect "first start allowed" zhack -d $BASEDIR raidz_epochs \
    $POOL 0 0:$WIDTH:1 $((txg + 1)):$WIDTH:2
get_table
[[ -z $table ]] || log_fail "a refused table was stored: $table"
check_blocks

get_txg
out=$(zhack -d $BASEDIR raidz_epochs $POOL 0 0:$WIDTH:1 next:$WIDTH:2) ||
    log_fail "zhack refused a table that starts at the next txg allowed"
log_note "$out"
last=${out##* }
[[ ${last%%:*} == +([0-9]) ]] || log_fail "no epoch start in: $out"
start=${last%%:*}
(( start > txg )) ||
    log_fail "the epoch starts at $start, not after txg $txg of the pool"
get_table
[[ $table == "0:$WIDTH:1 $start:$WIDTH:2" ]] ||
    log_fail "zdb shows another table: $table"
check_blocks

out=$(raidz_epoch_test write $BASEDIR $POOL 8192) ||
    log_fail "write failed: $out"
log_note "$out"
last=$(print -r -- "$out" | sed -n 's/.* birth=\([0-9]*\).*/\1/p')
[[ $last == +([0-9]) ]] || log_fail "no birth txg in: $out"
(( birth = last ))
(( birth >= start )) || log_fail "a block written afterwards was born" \
    "in txg $birth, before the epoch at $start"

# The blocks of the epoch exist now: it cannot be dropped or changed.
log_mustnot_expect "not valid" zhack -d $BASEDIR raidz_epochs $POOL 0 \
    0:$WIDTH:1
log_mustnot_expect "not valid" zhack -d $BASEDIR raidz_epochs $POOL 0 \
    0:$WIDTH:1 $((start + 1)):$WIDTH:2
log_mustnot_expect "not valid" zhack -d $BASEDIR raidz_epochs $POOL 0 \
    0:$WIDTH:1 next:$WIDTH:3
get_table
[[ $table == "0:$WIDTH:1 $start:$WIDTH:2" ]] ||
    log_fail "a refused table changed the stored one: $table"

out=$(zhack -d $BASEDIR raidz_epochs $POOL 0 0:$WIDTH:1 \
    $start:$WIDTH:2 next:$WIDTH:3) ||
    log_fail "zhack refused a table that appends an epoch"
log_note "$out"
last=${out##* }
get_table
[[ $table == "0:$WIDTH:1 $start:$WIDTH:2 $last" ]] ||
    log_fail "zdb shows another table: $table"
check_blocks

# The first start allowed, seen from the sync task that stores the table.
log_must mkdir -p $BASEDIR/boundary
log_must raidz_epoch_test create $BASEDIR/boundary $POOL $WIDTH 1
out=$(raidz_epoch_test boundary $BASEDIR/boundary $POOL) ||
    log_fail "boundary failed: $out"
log_note "$out"
task=$(print -r -- "$out" |
    sed -n 's/^RESULT boundary rc=0 txg=\([0-9]*\)$/\1/p')
[[ $task == +([0-9]) ]] || log_fail "no txg for the sync task in: $out"
for d in 0 1 2; do
	[[ $out == *"start=T+$d check=ERANGE set=ERANGE"* ]] ||
	    log_fail "an epoch that starts at T+$d was not refused"
done
[[ $out == *"start=T+3 check=accepted set=stored"* ]] ||
    log_fail "an epoch that starts at T+3 was not stored"
[[ $out == *"table=0:$WIDTH:1,$((task + 3)):$WIDTH:2"* ]] ||
    log_fail "the vdev does not have the epoch that starts at T+3"
log_must eval "zdb -e -p $BASEDIR/boundary -bcc $POOL > $BASEDIR/zdb.out 2>&1"
log_must grep -q "No leaks" $BASEDIR/zdb.out

log_pass "a parity-epoch table only grows, past the txgs already written"
