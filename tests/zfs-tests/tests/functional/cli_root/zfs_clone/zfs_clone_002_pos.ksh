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

	if datasetexists $TESTPOOL/notexist ; then
		log_must zfs destroy -rRf $TESTPOOL/notexist
	fi

	for snap in $SNAPFS $SNAPFS1 ; do
		if snapexists $snap ; then
			log_must zfs destroy -Rf $snap
		fi
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
