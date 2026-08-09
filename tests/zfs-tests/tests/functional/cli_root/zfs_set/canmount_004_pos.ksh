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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify canmount=noauto work fine when setting sharenfs or sharesmb.
#
# STRATEGY:
# 1. Create a fs canmount=noauto.
# 2. Set sharenfs or sharesmb.
# 3. Verify the fs is umounted.
#

verify_runnable "global"

# properties
set -A sharenfs_prop "off" "on" "ro"
set -A sharesmb_prop "off" "on"

function cleanup
{
	log_must zfs destroy -rR $CS_FS
}

function assert_unmounted
{
	mnted=$(get_prop mounted $CS_FS)
	if [[ "$mnted" == "yes" ]]; then
		canmnt=$(get_prop canmount $CS_FS)
		shnfs=$(get_prop sharenfs $CS_FS)
		shsmb=$(get_prop sharesmb $CS_FS)
		mntpt=$(get_prop mountpoint $CS_FS)
		log_fail "$CS_FS should be unmounted" \
		"[canmount=$canmnt,sharenfs=$shnfs,sharesmb=$shsmb,mountpoint=$mntpt]."
	fi
}

log_assert "Verify canmount=noauto work fine when setting sharenfs or sharesmb."
log_onexit cleanup

CS_FS=$TESTPOOL/$TESTFS/cs_fs.$$
oldmpt=$TESTDIR/old_cs_fs.$$
newmpt=$TESTDIR/new_cs_fs.$$

log_must zfs create -o canmount=noauto -o mountpoint=$oldmpt $CS_FS
assert_unmounted

for n in ${sharenfs_prop[@]}; do
	log_must zfs set sharenfs="$n" $CS_FS
	assert_unmounted
	for s in ${sharesmb_prop[@]}; do
		log_must zfs set sharesmb="$s" $CS_FS
		assert_unmounted

		mntpt=$(get_prop mountpoint $CS_FS)
		if [[ "$mntpt" == "$oldmpt" ]]; then
			log_must zfs set mountpoint="$newmpt" $CS_FS
		else
			log_must zfs set mountpoint="$oldmpt" $CS_FS
		fi
		assert_unmounted
	done
done

log_pass "Verify canmount=noauto work fine when setting sharenfs or sharesmb."

