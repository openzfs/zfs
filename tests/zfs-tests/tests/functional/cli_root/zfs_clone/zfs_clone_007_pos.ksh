#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs clone -o version=' could upgrade version, but downgrade is denied.
#
# STRATEGY:
# 1. Create clone with "-o version=" specified
# 2. Verify it succeed while upgrade, but fails while the version downgraded.
#

ZFS_VERSION=$(zfs upgrade | grep -wom1 '[[:digit:]]*')

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS && destroy_dataset $SNAPFS -Rf
}

log_onexit cleanup

log_assert "'zfs clone -o version=' could upgrade version," \
	"but downgrade is denied."

log_must zfs snapshot $SNAPFS

typeset -i ver

if (( ZFS_TEST_VERSION == 0 )) ; then
	(( ZFS_TEST_VERSION = ZFS_VERSION ))
fi

(( ver = ZFS_TEST_VERSION ))
while (( ver <= ZFS_VERSION )); do
	log_must zfs clone -o version=$ver $SNAPFS $TESTPOOL/$TESTCLONE
	cleanup
	(( ver = ver + 1 ))
done

(( ver = 0 ))
while (( ver < ZFS_TEST_VERSION  )); do
	log_mustnot zfs clone -o version=$ver \
		$SNAPFS $TESTPOOL/$TESTCLONE
	log_mustnot datasetexists $TESTPOOL/$TESTCLONE
	cleanup
	(( ver = ver + 1 ))
done

log_pass "'zfs clone -o version=' could upgrade version," \
	"but downgrade is denied."
