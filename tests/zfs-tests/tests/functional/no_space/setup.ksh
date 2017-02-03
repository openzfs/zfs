#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

verify_runnable "global"

if ! $(is_physical_device $DISKS) ; then
	log_unsupported "This directory cannot be run on raw files."
fi

DISK=${DISKS%% *}

log_must set_partition 0 "" $SIZE $DISK

if is_linux; then
	if ( is_mpath_device $DISK ) && [[ -z $($ECHO $DISK | awk 'substr($1,18,1)\
	    ~ /^[[:digit:]]+$/') ]] || ( is_real_device $DISK ); then
		default_setup $DISK"1"
	elif ( is_mpath_device $DISK || is_loop_device $DISK ); then
		default_setup $DISK"p1"
	else
		log_fail "$DISK not supported for partitioning."
	fi
else
	default_setup $DISK"s0"
fi
