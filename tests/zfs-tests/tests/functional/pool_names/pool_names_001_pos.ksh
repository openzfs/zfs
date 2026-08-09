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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Test that a set of valid names can be used to create pools. Further
# verify that the created pools can be destroyed.
#
# STRATEGY:
# 1) For each valid character in the character set, try to create
# and destroy the pool.
# 2) Given a list of valid pool names, try to create and destroy
# pools with the given names.
#

verify_runnable "global"

log_assert "Ensure that pool names can use the ASCII subset of UTF-8"

function cleanup
{
	if [[ -n $name ]] && poolexists $name ; then
		log_must zpool destroy $name
	fi

	if [[ -d $TESTDIR ]]; then
		log_must rm -rf $TESTDIR
	fi

}

log_onexit cleanup

DISK=${DISKS%% *}
if [[ ! -e $TESTDIR ]]; then
	log_must mkdir $TESTDIR
fi

log_note "Ensure letters of the alphabet are allowable"

typeset name=""

for name in A B C D E F G H I J K L M \
    N O P Q R S T U V W X Y Z \
    a b c d e f g h i j k l m \
    n o p q r s t u v w x y z
do
	log_must zpool create -m $TESTDIR $name $DISK
	if ! poolexists $name; then
		log_fail "Could not create a pool called '$name'"
	fi

	log_must zpool destroy $name
done

log_note "Ensure a variety of unusual names passes"

name=""

for name in "a.............................." "a_" "a-" "a:" \
    "a." "a123456" "bc0t0d0" "m1rr0r_p00l" "ra1dz_p00l" \
    "araidz2" "C0t2d0" "cc0t0" "raid2:-_." "mirr_:-." \
    "m1rr0r-p00l" "ra1dz-p00l" "spar3_p00l" \
    "spar3-p00l" "hiddenmirrorpool" "hiddenraidzpool" \
    "hiddensparepool"
do
	log_must zpool create -m $TESTDIR $name $DISK
	if ! poolexists $name; then
		log_fail "Could not create a pool called '$name'"
	fi

	#
	# Since the naming convention applies to datasets too,
	# create datasets with the same names as above.
	#
	log_must zfs create $name/$name
	log_must zfs snapshot $name/$name@$name
	log_must zfs clone $name/$name@$name $name/clone_$name
	log_must zfs create -V 150m $name/$name/$name
	block_device_wait

	log_must zpool destroy $name
done

log_pass "Valid pool names were accepted correctly."
