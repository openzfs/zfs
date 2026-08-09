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
# Copyright 2016, loli10K. All rights reserved.
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	'zpool create -o ashift=<n> ...' should work with different ashift
#	values.
#
# STRATEGY:
#	1. Create various pools with different ashift values.
#	2. Verify -o ashift=<n> works only with allowed values (9-16).
#	   Also verify that the lowest number of uberblocks in a label is 16 and
#	   smallest uberblock size is 8K even with higher ashift values.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $disk
}

#
# Fill the uberblock ring in every <device> label: we do this by committing
# TXGs to the provided <pool> until every slot contains a valid uberblock.
# NOTE: We use 'zpool sync' here because we can't force it via sync(1) like on
# illumos
#
function write_device_uberblocks # <device> <pool>
{
	typeset device=$1
	typeset pool=$2

	while zdb -quuul $device | grep -q 'invalid'; do
		sync_pool $pool true
	done
}

#
# Verify every label on <device> contains <count> (valid) uberblocks
#
function verify_device_uberblocks # <device> <count>
{
	typeset device=$1
	typeset ubcount=$2

	zdb -quuul $device | awk -v ubcount=$ubcount '
	    /Uberblock/ && ! /invalid/ { uberblocks[$0]++ }
	    END {
	        count = 0
	        for (i in uberblocks) {
	            if (uberblocks[i] != 4) {
	                printf "%s count: %s != 4\n", i, uberblocks[i]
	                exit 1
	            }
	            count++;
	        }
	        if (count != ubcount) {
	            printf "Total uberblock count: %s != %s\n", count, ubcount
	            exit 1
	        }
	    }'
}

log_assert "zpool create -o ashift=<n>' works with different ashift values"
log_onexit cleanup

disk=$(create_blockfile $SIZE)

typeset ashifts=("9" "10" "11" "12" "13" "14" "15" "16")
# since Illumos 4958 the largest uberblock is 8K so we have at least of 16/label
typeset ubcount=("128" "128" "64" "32" "16" "16" "16" "16")
typeset -i i=0;
while [ $i -lt "${#ashifts[@]}" ]
do
	typeset ashift=${ashifts[$i]}
	log_must zpool create -o ashift=$ashift $TESTPOOL $disk
	typeset pprop=$(get_pool_prop ashift $TESTPOOL)
	verify_ashift $disk $ashift
	if [[ $? -ne 0 || "$pprop" != "$ashift" ]]
	then
		log_fail "Pool was created without setting ashift value to "\
		    "$ashift (current = $pprop)"
	fi
	write_device_uberblocks $disk $TESTPOOL
	log_must verify_device_uberblocks $disk ${ubcount[$i]}

	# clean things for the next run
	log_must zpool destroy $TESTPOOL
	log_must zpool labelclear $disk
	log_must verify_device_uberblocks $disk 0
	((i = i + 1))
done

typeset badvals=("off" "on" "1" "8" "17" "1b" "ff" "-")
for badval in ${badvals[@]}
do
	log_mustnot zpool create -o ashift="$badval" $TESTPOOL $disk
done

log_pass "zpool create -o ashift=<n>' works with different ashift values"
