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
