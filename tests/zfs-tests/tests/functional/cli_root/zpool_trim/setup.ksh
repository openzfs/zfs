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

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if is_freebsd; then
	log_unsupported "FreeBSD has no hole punching mechanism for the time being."
	diskinfo -v $DISKS | grep -qE 'No.*# TRIM/UNMAP support' &&
	    log_unsupported "DISKS do not support discard (TRIM/UNMAP)"
else
	DISK1=${DISKS%% *}

	typeset -i max_discard=0
	if is_disk_device $DEV_RDSKDIR/$DISK1; then
		max_discard=$(lsblk -Dbn $DEV_RDSKDIR/$DISK1 | awk '{ print $4; exit }')
	fi

	if test $max_discard -eq 0; then
		log_unsupported "DISKS do not support discard (TRIM/UNMAP)"
	fi
fi

log_pass
