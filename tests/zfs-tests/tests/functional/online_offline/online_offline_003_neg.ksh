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
# Offlining disks in a non-redundant pool should fail.
#
# STRATEGY:
# 1. Create a multidisk stripe and start some random I/O
# 2. zpool offline should fail on each disk.
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi

	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Offlining disks in a non-redundant pool should fail."

log_onexit cleanup

specials_list=""
for i in 0 1 2; do
	log_must mkfile $MINVDEVSIZE $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"
done
disk=($specials_list)

create_pool $TESTPOOL1 $specials_list
log_must zfs create $TESTPOOL1/$TESTFS1
log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

for i in 0 1 2; do
	log_mustnot zpool offline $TESTPOOL1 ${disk[$i]}
	check_state $TESTPOOL1 ${disk[$i]} "online"
done

log_pass
