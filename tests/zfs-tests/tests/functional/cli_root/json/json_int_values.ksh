#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.

# Copyright (c) 2026 by Tigerblue77.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# --json-int prints a property value as a number only when it is made of
# digits, has no leading zero and fits in a uint64: such a value is printed
# exactly, also above INT64_MAX, and an empty or longer value, or one with a
# leading zero, stays a string.
#
# STRATEGY:
# 1. Give testpool1 a guid above INT64_MAX and check it in 'zpool get',
#    'zpool get' of the root vdev and 'zpool list'.
# 2. Set dataset and pool user properties to a value above INT64_MAX,
#    a run of digits longer than a uint64, a value with a leading zero,
#    "0" and an empty value, and check each in 'zfs get', 'zfs list' and
#    'zpool get'.
# The values are read from the JSON text: jq 1.6 rounds large integers.
#

verify_runnable "global"

BIG=18446744073709551557
LONG=99999999999999999999999
LEAD=007

function cleanup
{
	typeset prop

	for prop in org.test:big org.test:long org.test:lead org.test:zero \
	    org.test:empty; do
		log_must zfs inherit $prop $TESTPOOL1
	done
	for prop in org.test:big org.test:long org.test:lead org.test:zero; do
		log_must zpool set $prop= $TESTPOOL1
	done
}

#
# Check that the value of property $2 in the JSON output of the command
# that follows is the JSON text $1.
#
function check_value
{
	typeset expected=$1
	typeset prop=$2
	shift 2
	typeset out
	typeset got

	out=$("$@") || log_fail "$* failed"
	echo "$out" | jq -e . > /dev/null || log_fail "$*: invalid JSON: $out"
	got=$(echo "$out" | sed -n "s/.*\"$prop\":{\"value\":\([^,}]*\).*/\1/p")
	if [[ "$got" != "$expected" ]]; then
		log_fail "$*: $prop is '$got', expected '$expected'"
	fi
	log_note "$*: $prop is $got"
}

log_onexit cleanup

log_must zpool reguid -g $BIG $TESTPOOL1
log_must test "$(zpool get -Hp -o value guid $TESTPOOL1)" = $BIG
log_must test "$(zpool get -Hp -o value guid $TESTPOOL1 root)" = $BIG
check_value $BIG guid zpool get -j --json-int guid $TESTPOOL1
check_value $BIG guid zpool get -j --json-int guid $TESTPOOL1 root
check_value $BIG guid zpool list -j --json-int -o guid $TESTPOOL1

log_must zfs set org.test:big=$BIG $TESTPOOL1
log_must zfs set org.test:long=$LONG $TESTPOOL1
log_must zfs set org.test:lead=$LEAD $TESTPOOL1
log_must zfs set org.test:zero=0 $TESTPOOL1
log_must zfs set org.test:empty= $TESTPOOL1
log_must test "$(zfs get -H -o source org.test:empty $TESTPOOL1)" = local
for cmd in "zfs get -j --json-int" "zfs list -j --json-int -o"; do
	check_value $BIG org.test:big $cmd org.test:big $TESTPOOL1
	check_value "\"$LONG\"" org.test:long $cmd org.test:long $TESTPOOL1
	check_value "\"$LEAD\"" org.test:lead $cmd org.test:lead $TESTPOOL1
	check_value 0 org.test:zero $cmd org.test:zero $TESTPOOL1
	check_value '""' org.test:empty $cmd org.test:empty $TESTPOOL1
done

# An empty pool user property is removed, so only a dataset can hold one.
log_must zpool set org.test:big=$BIG $TESTPOOL1
log_must zpool set org.test:long=$LONG $TESTPOOL1
log_must zpool set org.test:lead=$LEAD $TESTPOOL1
log_must zpool set org.test:zero=0 $TESTPOOL1
check_value $BIG org.test:big zpool get -j --json-int org.test:big $TESTPOOL1
check_value "\"$LONG\"" org.test:long \
    zpool get -j --json-int org.test:long $TESTPOOL1
check_value "\"$LEAD\"" org.test:lead \
    zpool get -j --json-int org.test:lead $TESTPOOL1
check_value 0 org.test:zero zpool get -j --json-int org.test:zero $TESTPOOL1

log_pass "--json-int prints as numbers only digits that fit in a uint64" \
    "and have no leading zero"
