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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs clone -p' should work as expected
#
# STRATEGY:
#	1. prepare snapshots
#	2. make sure without -p option, 'zfs clone' will fail
#	3. with -p option, the clone can be created
#	4. run 'zfs clone -p' again, the exit code should be zero
#

verify_runnable "both"

function setup_all
{
	log_note "Create snapshots and mount them..."

	for snap in $SNAPFS $SNAPFS1 ; do
		if ! snapexists $snap ; then
			log_must zfs snapshot $snap
		fi
	done

	return 0
}

function cleanup_all
{

	datasetexists $TESTPOOL/notexist && destroy_dataset $TESTPOOL/notexist -rRf

	for snap in $SNAPFS $SNAPFS1 ; do
		snapexists $snap && destroy_dataset $snap -Rf
	done

	return 0
}

log_assert "clone -p should work as expected."
log_onexit cleanup_all

setup_all

log_must verify_opt_p_ops "clone" "fs" $SNAPFS \
	 $TESTPOOL/notexist/new/clonefs$$

if is_global_zone ; then
	log_must verify_opt_p_ops "clone" "vol" $SNAPFS1 \
		 $TESTPOOL/notexist/new/clonevol$$
fi

log_pass "clone -p should work as expected."
