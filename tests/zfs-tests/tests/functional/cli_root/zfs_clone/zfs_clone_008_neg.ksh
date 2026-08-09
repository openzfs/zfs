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
# 'zfs clone -o <filesystem>' fails with bad <filesystem> arguments, including:
#	*Same property set multiple times via '-o property=value'
#	*Volume's property set on filesystem
#
# STRATEGY:
# 1. Create an array of <filesystem> arguments
# 2. Execute 'zfs clone -o <filesystem>' with each argument
# 3. Verify an error is returned.
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS && destroy_dataset $SNAPFS -Rf
}

log_onexit cleanup

log_assert "Verify 'zfs clone -o <filesystem>' fails with bad <filesystem> argument."

log_must zfs snapshot $SNAPFS

typeset -i i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_mustnot zfs clone -o ${RW_FS_PROP[i]} -o ${RW_FS_PROP[i]} \
		$SNAPFS $TESTPOOL/$TESTCLONE
	log_mustnot zfs clone -p -o ${RW_FS_PROP[i]} -o ${RW_FS_PROP[i]} \
		$SNAPFS $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

i=0
while (( $i < ${#VOL_ONLY_PROP[*]} )); do
	log_mustnot zfs clone -o ${VOL_ONLY_PROP[i]} \
		$SNAPFS $TESTPOOL/$TESTCLONE
	log_mustnot zfs clone -p -o ${VOL_ONLY_PROP[i]} \
		$SNAPFS $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

log_pass "'zfs clone -o <filesystem>' fails with bad <filesystem> argument."
