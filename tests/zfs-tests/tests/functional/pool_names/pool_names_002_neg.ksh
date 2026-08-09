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
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Ensure that a set of invalid names cannot be used to create pools.
#
# STRATEGY:
# 1) For each invalid character in the character set, try to create
# and destroy the pool. Verify it fails.
# 2) Given a list of invalid pool names, ensure the pools are not
# created.
#

verify_runnable "global"

log_assert "Ensure that a set of invalid names cannot be used to create pools."

# Global variable use to cleanup failures.
POOLNAME=""

function cleanup
{
	if poolexists $POOLNAME; then
		log_must zpool destroy $POOLNAME
	fi

	if [[ -d $TESTDIR ]]; then
		log_must rm -rf $TESTDIR
	fi
}

log_onexit cleanup

for pool in $(get_all_pools); do
	if [[ "$pool" != "$TESTPOOL" ]]; then
		log_must zpool destroy $pool
	fi
done

DISK=${DISKS%% *}
if [[ ! -e $TESTDIR ]]; then
        log_must mkdir $TESTDIR
fi

log_note "Ensure invalid characters fail"
for POOLNAME in "!" "\"" "#" "$" "%" "&" "'" "(" ")" \
    "\*" "+" "," "-" "\." "/" "\\" \
    0 1 2 3 4 5 6 7 8 9 \
    ":" ";" "<" "=" ">" "\?" "@" \
    "[" "]" "^" "_" "\`" "{" "|" "}" "~"
do
	log_mustnot zpool create -m $TESTDIR $POOLNAME $DISK
        if poolexists $POOLNAME; then
                log_fail "Unexpectedly created pool: '$POOLNAME'"
        fi

	log_mustnot zpool destroy $POOLNAME
done

log_note "Check that invalid octal values fail"
for oct in "\000" "\001" "\002" "\003" "\004" "\005" "\006" "\007" \
    "\010" "\011" "\012" "\013" "\014" "\015" "\017" \
    "\020" "\021" "\022" "\023" "\024" "\025" "\026" "\027" \
    "\030" "\031" "\032" "\033" "\034" "\035" "\036" "\037" \
    "\040" "\177"
do
	POOLNAME=`eval "echo x | tr 'x' '$oct'"`
	log_mustnot zpool create -m $TESTDIR $POOLNAME $DISK
        if poolexists $POOLNAME; then
                log_fail "Unexpectedly created pool: '$POOLNAME'"
        fi

	log_mustnot zpool destroy $POOLNAME
done

log_note "Verify invalid pool names fail"
set -A POOLNAME \
    "mirror" "raidz" ",," ",,,,,,,,,,,,,,,,,,,,,,,,," \
    "2222222222222222222" "mirror_pool" "raidz_pool" \
    "mirror-pool" "raidz-pool" "spare" "spare_pool" \
    "spare-pool" "raidz1-" "raidz2:" ":aaa" "-bbb" "_ccc" ".ddd"

POOLNAME[${#POOLNAME[@]}]='log'

typeset -i i=0
while ((i < ${#POOLNAME[@]})); do
	log_mustnot zpool create -m $TESTDIR ${POOLNAME[$i]} $DISK
        if poolexists ${POOLNAME[$i]}; then
                log_fail "Unexpectedly created pool: '${POOLNAME[$i]}'"
        fi

	log_mustnot zpool destroy ${POOLNAME[$i]}

	((i += 1))
done

log_pass "Invalid names and characters were caught correctly"
