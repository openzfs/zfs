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
#	The number of failures a raidz vdev survives is its parity, or less
#	if it holds blocks of a lower parity.  A parity-epoch table whose
#	last epoch has a higher parity does not raise it: a raidz1 vdev with
#	a table that ends at parity 2 must not survive two failures.  The
#	completion marker of an in-place re-parity, which software that has
#	such an operation would write next to such a table, is given to pool
#	creation too, and does not reach the labels.
#
# STRATEGY:
#	1. Create a raidz1 pool from a vdev config that carries a completion
#	   marker for parity 2 and the digest of the table that goes with it,
#	   and store that table, which ends at parity 2, as zhack does.  The
#	   labels must carry the table, and not the marker.
#	2. Fault two children of the open pool: the vdev has to go from
#	   DEGRADED to CANT_OPEN, as it does in a pool without a table.
#	3. Import the pool with two children missing: it must not open.
#	4. With one child missing it opens, DEGRADED.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_tolerance_marker.$$
typeset -r POOL=${TESTPOOL}_marker
typeset -i WIDTH=5

function cleanup
{
	rm -rf $BASEDIR
}

#
# marker_case <name> <marker parity, or none>
#
function marker_case
{
	typeset name=$1 marker=$2
	typeset dir=$BASEDIR/$name out

	log_note "case $name: raidz1, marker: $marker"
	log_must mkdir -p $dir
	if [[ $marker == none ]]; then
		log_must raidz_epoch_test create $dir $POOL $WIDTH 1
	else
		log_must raidz_epoch_test create $dir $POOL $WIDTH 1 $marker
		log_must eval "zdb -l $dir/v0 > $dir/labels.out"
		log_must grep -q 'raidz_parity_epochs\[' $dir/labels.out
		log_mustnot grep -q 'rrmvp' $dir/labels.out
	fi

	out=$(raidz_epoch_test state $dir $POOL fault) ||
	    log_fail "state fault did not complete: $out"
	log_note "$out"
	[[ $out == *"fault1 top=DEGRADED"* ]] ||
	    log_fail "case $name: one faulted child did not leave it DEGRADED"
	[[ $out == *"fault2 top=CANT_OPEN"* ]] ||
	    log_fail "case $name: two faulted children did not make it" \
	    "CANT_OPEN"

	# One child gone: still redundant.
	log_must mkdir -p $dir.away
	log_must mv $dir/v1 $dir.away/
	out=$(raidz_epoch_test state $dir $POOL open) ||
	    log_fail "state open did not complete: $out"
	log_note "$out"
	[[ $out == *"import rc=0"* && $out == *"top=DEGRADED"* ]] ||
	    log_fail "case $name: one missing child, pool did not open"

	# Two children gone: not redundant enough.
	log_must mv $dir/v3 $dir.away/
	out=$(raidz_epoch_test state $dir $POOL open) ||
	    log_fail "state open did not complete: $out"
	log_note "$out"
	[[ $out == *"import rc=0"* ]] &&
	    log_fail "case $name: two missing children, pool opened"
}

log_onexit cleanup
log_assert "a table that ends at a higher parity does not raise the tolerance"

marker_case plain none
marker_case marked 2

log_pass "a table that ends at a higher parity does not raise the tolerance"
