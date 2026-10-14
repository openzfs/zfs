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
#	A parity-epoch table cannot gain an epoch whose parity is lower than
#	the number of children the raidz vdev cannot read or write.  Blocks
#	born in that epoch could not be written while those children are
#	missing, so the pool would suspend, and the vdev, which tolerates
#	no more missing children than the lowest parity of its table, could
#	not be opened.
#
# STRATEGY:
#	1. Create a six-wide raidz2 pool on file vdevs and hide two of its
#	   children.
#	2. Require zhack to refuse an epoch of parity 1, say why, and store
#	   nothing.
#	3. Bring one child back, and require zhack to store the same epoch,
#	   whose parity covers the one child still missing.
#	4. Bring the other child back, and require zdb to show the table and
#	   to read every block of the pool.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_degraded.$$
typeset -r POOL=${TESTPOOL}_degraded
typeset -i WIDTH=6
typeset out table

function cleanup
{
	rm -rf $BASEDIR
}

# Set table to the table of vdev 0 that zdb prints, as zhack takes it.
function get_table
{
	log_must eval "zdb -e -p $BASEDIR -m $POOL > $BASEDIR/zdb.out"
	table=$(sed -n 's/.*raidz_parity_epochs [0-9]*: //p' $BASEDIR/zdb.out |
	    sed 's/\[txg>=\([0-9]*\) w=\([0-9]*\) p=\([0-9]*\)\]/\1:\2:\3/g')
}

log_onexit cleanup
log_assert "an epoch cannot have less parity than the children missing"

log_must mkdir -p $BASEDIR/away
log_must raidz_epoch_test create $BASEDIR $POOL $WIDTH 2
log_must mv $BASEDIR/v0 $BASEDIR/v1 $BASEDIR/away

# Without the check the table is stored, and zhack never returns: the pool
# suspends on the first block of the new epoch.
log_mustnot_expect "cannot read or write" timeout 300 zhack -d $BASEDIR \
    raidz_epochs $POOL 0 0:$WIDTH:2 next:$WIDTH:1
get_table
[[ -z $table ]] || log_fail "a refused table was stored: $table"

log_must mv $BASEDIR/away/v1 $BASEDIR
out=$(timeout 300 zhack -d $BASEDIR raidz_epochs $POOL 0 0:$WIDTH:2 \
    next:$WIDTH:1) ||
    log_fail "zhack refused an epoch that covers the child missing: $out"
log_note "$out"
get_table
[[ $table == "0:$WIDTH:2 ${out##* }" ]] ||
    log_fail "zdb shows another table: $table"

log_must mv $BASEDIR/away/v0 $BASEDIR
get_table
[[ $table == "0:$WIDTH:2 ${out##* }" ]] ||
    log_fail "zdb shows another table: $table"
log_must eval "zdb -e -p $BASEDIR -bcc $POOL > $BASEDIR/zdb.out 2>&1"
log_must grep -q "No leaks" $BASEDIR/zdb.out

log_pass "an epoch cannot have less parity than the children missing"
