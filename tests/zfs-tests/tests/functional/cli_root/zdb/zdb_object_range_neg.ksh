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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#
# Copyright (c) 2020 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# Description:
# A badly formed object range parameter passed to zdb -dd should
# return an error.
#
# Strategy:
# 1. Create a pool
# 2. Run zdb -dd with assorted invalid object range arguments and
#    confirm it fails as expected
# 3. Run zdb -dd with an invalid object identifier and
#    confirm it fails as expected

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Execute zdb using invalid object range parameters."
log_onexit cleanup
verify_runnable "both"
verify_disk_count "$DISKS" 2
default_mirror_setup_noexit $DISKS

sync_all_pools

set -A bad_flags a b c   e   g h i j k l   n o p q r s t u v w x y   \
                   B C D E F G H I J K L M N O P Q R S T U V W X Y Z \
                 0 1 2 3 4 5 6 7 8 9 _ - + % . , :

typeset -i i=0
while [[ $i -lt ${#bad_flags[*]} ]]; do
	log_mustnot zdb -dd $TESTPOOL 0:1:${bad_flags[i]}
	log_mustnot zdb -dd $TESTPOOL 0:1:A-${bad_flags[i]}
	((i = i + 1))
done

set -A bad_ranges ":" "::" ":::" ":0" "0:" "0:1:" "0:1::" "0::f" "0a:1" \
    "a0:1" "a:1" "0:a" "0:1a" "0:a1" "a:b0" "a:0b" "0:1:A-" "1:0" \
    "0:1:f:f" "0:1:f:"

i=0
while [[ $i -lt ${#bad_ranges[*]} ]]; do
	log_mustnot zdb -dd $TESTPOOL ${bad_ranges[i]}
	((i = i + 1))
done

# Specifying a non-existent object identifier returns an error
obj_id_highest=$(zdb -P -dd $TESTPOOL/$TESTFS 2>/dev/null |
    grep -E "^ +-?([0-9]+ +){7}" | sort -n | awk 'END {print $1}')
obj_id_invalid=$(( $obj_id_highest + 1 ))
log_mustnot zdb -dd $TESTPOOL/$TESTFS $obj_id_invalid

log_pass "Badly formed zdb object range parameters fail as expected."
