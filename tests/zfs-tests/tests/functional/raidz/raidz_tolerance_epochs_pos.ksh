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
#	What a raidz vdev claims about the failures it survives has to hold
#	for every block it can hold.  With a parity-epoch table that lowers
#	the parity, the vdev can hold blocks of a lower parity than its own,
#	so it must open, go from DEGRADED to CANT_OPEN, and allow children to
#	be offlined as the lowest parity of the table says, not as its base
#	parity says.  A table that only raises the parity changes nothing.
#
# STRATEGY:
#	For a raidz vdev with a parity-epoch table, and one without, check
#	against the number of failures T the lowest parity allows:
#	1. Two faulted children leave it DEGRADED if T is 2 and make it
#	   CANT_OPEN if T is 1.
#	2. A pool missing one child imports, and one missing two imports
#	   only if T is 2.
#	3. Of two children to offline, the second is refused if T is 1.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_tolerance_epochs.$$
typeset -r POOL=${TESTPOOL}_tol
typeset -i WIDTH=5

function cleanup
{
	rm -rf $BASEDIR
}

#
# tolerance_case <name> <vdev parity> <table: none|<parity after the epoch>>
#	<failures survived>
#
function tolerance_case
{
	typeset name=$1 base=$2 after=$3 tol=$4
	typeset dir=$BASEDIR/$name out
	typeset -i txg

	log_note "case $name: raidz$base vdev, epoch table: $after," \
	    "survives $tol failures"
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH $base

	#
	# An epoch that starts well after the pool, so that every block is of
	# the first parity and only the table can say anything about the rest.
	#
	if [[ $after != none ]]; then
		log_must eval "zdb -lu $dir/v0 > $dir/labels.out"
		txg=$(awk '/txg = /{print $3}' $dir/labels.out |
		    sort -n | tail -1)
		(( txg > 0 )) || log_fail "case $name: no txg in $dir/v0 labels"
		log_must zhack -d $dir raidz_epochs $POOL 0 \
		    0:$WIDTH:$base $((txg + 1000)):$WIDTH:$after
	fi

	# 1. Faulting two children.
	out=$(raidz_epoch_test state $dir $POOL fault) ||
	    log_fail "state fault did not complete: $out"
	log_note "$out"
	[[ $out == *"fault1 top=DEGRADED"* ]] ||
	    log_fail "case $name: one faulted child did not leave it DEGRADED"
	if (( tol == 2 )); then
		[[ $out == *"fault2 top=DEGRADED"* ]] ||
		    log_fail "case $name: two faulted children were not" \
		    "tolerated"
	else
		[[ $out == *"fault2 top=CANT_OPEN"* ]] ||
		    log_fail "case $name: two faulted children were tolerated"
	fi

	# 2. Importing with children missing.
	log_must mkdir -p $dir.away
	log_must mv $dir/v1 $dir.away/
	out=$(raidz_epoch_test state $dir $POOL open) ||
	    log_fail "state open did not complete: $out"
	log_note "$out"
	[[ $out == *"import rc=0"* && $out == *"top=DEGRADED"* ]] ||
	    log_fail "case $name: one missing child, pool did not open"
	log_must mv $dir/v3 $dir.away/
	out=$(raidz_epoch_test state $dir $POOL open) ||
	    log_fail "state open did not complete: $out"
	log_note "$out"
	if (( tol == 2 )); then
		[[ $out == *"import rc=0"* && $out == *"top=DEGRADED"* ]] ||
		    log_fail "case $name: two missing children, did not open"
	else
		[[ $out == *"import rc=0"* ]] &&
		    log_fail "case $name: two missing children, pool opened"
	fi
	log_must mv $dir.away/v1 $dir.away/v3 $dir/

	# 3. Offlining two children, which persists, so it comes last.
	out=$(raidz_epoch_test state $dir $POOL offline) ||
	    log_fail "state offline did not complete: $out"
	log_note "$out"
	[[ $out == *"offline first=0 "* ]] ||
	    log_fail "case $name: the first child could not be offlined"
	if (( tol == 2 )); then
		[[ $out == *"second=0"* ]] ||
		    log_fail "case $name: the second child was refused"
	else
		[[ $out == *"second=0"* ]] &&
		    log_fail "case $name: the second child was offlined"
	fi
}

log_onexit cleanup
log_assert "a raidz vdev claims the redundancy of its weakest parity"

# No table: the parity of the vdev, one failure or two.
tolerance_case plain1 1 none 1
tolerance_case plain2 2 none 2

# A table that raises the parity changes nothing.
tolerance_case raise 1 2 1

# A table that lowers it: the lowest parity is what every block survives.
tolerance_case lower 2 1 1

log_pass "a raidz vdev claims the redundancy of its weakest parity"
