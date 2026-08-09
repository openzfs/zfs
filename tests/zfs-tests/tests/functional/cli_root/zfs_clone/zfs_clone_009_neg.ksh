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
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs clone -o <volume>' fails with badly formed arguments,including:
#       *Same property set multiple times via '-o property=value'
#       *Filesystems's property set on volume
#
# STRATEGY:
# 1. Create an array of badly formed arguments
# 2. For each argument, execute 'zfs clone -o <volume>'
# 3. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	snapexists $SNAPFS1 && destroy_dataset $SNAPFS1 -Rf
}

log_onexit cleanup

log_assert "Verify 'zfs clone -o <volume>' fails with bad <volume> argument."

log_must zfs snapshot $SNAPFS1

typeset -i i=0
while (( $i < ${#RW_VOL_PROP[*]} )); do
	log_mustnot zfs clone -o ${RW_VOL_PROP[i]} -o ${RW_VOL_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	log_mustnot zfs clone -p -o ${RW_VOL_PROP[i]} -o ${RW_VOL_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

i=0
while (( $i < ${#FS_ONLY_PROP[*]} )); do
	log_mustnot zfs clone  -o ${FS_ONLY_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	log_mustnot zfs clone -p -o ${FS_ONLY_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

log_pass "Verify 'zfs clone -o <volume>' fails with bad <volume> argument."
