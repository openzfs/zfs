#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that NFS share options are propagated correctly.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each option in the list, set the sharenfs property.
# 3. Verify through the share command that the options are propagated.
#

verify_runnable "global"

function cleanup
{
	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	is_shared $TESTPOOL/$TESTFS && \
		log_must unshare_fs $TESTPOOL/$TESTFS
}

if is_linux; then
	set -A shareopts \
	    "ro" "rw" "rw,insecure" "rw,async" "ro,crossmnt"
else
	set -A shareopts \
	    "ro" "ro=machine1" "ro=machine1:machine2" \
	    "rw" "rw=machine1" "rw=machine1:machine2" \
	    "ro=machine1:machine2,rw" "anon=0" "anon=0,sec=sys,rw" \
	    "nosuid" "root=machine1:machine2" "rw=.mydomain.mycompany.com" \
	    "rw=-terra:engineering" "log" "public"
fi

log_assert "Verify that NFS share options are propagated correctly."
log_onexit cleanup

cleanup

typeset -i i=0
while (( i < ${#shareopts[*]} ))
do
	log_must zfs set sharenfs="${shareopts[i]}" $TESTPOOL/$TESTFS

	option=$(get_prop sharenfs $TESTPOOL/$TESTFS)
	if [[ $option != ${shareopts[i]} ]]; then
		log_fail "get sharenfs failed. ($option != ${shareopts[i]})"
	fi

	# Verify the single option after the leading 'ro' or 'rw'.
	if is_linux; then
		IFS=',' read -r _ option _ <<<"$option"
	fi

	log_must eval "showshares_nfs | grep -q \"$option\""

	((i = i + 1))
done

log_pass "NFS options were propagated correctly."
