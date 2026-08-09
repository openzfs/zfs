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
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create -o property=value -V size volume' can successfully create a ZFS
# volume with multiple properties set.
#
# STRATEGY:
# 1. Create a ZFS volume in the storage pool with -o option
# 2. Verify the volume created successfully
# 3. Verify the properties are correctly set
#

verify_runnable "global"

function cleanup
{
	destroy_dataset $TESTPOOL/$TESTVOL
	destroy_dataset $TESTPOOL/$TESTVOL1
}

log_onexit cleanup


log_assert "'zfs create -o property=value -V size volume' can successfully \
	   create a ZFS volume with correct property set."

typeset -i i=0
typeset opts=""

while (( $i < ${#RW_VOL_PROP[*]} )); do
	if [[ ${RW_VOL_PROP[$i]} != *"checksum"* ]]; then
		opts="$opts -o ${RW_VOL_PROP[$i]}"
	fi
	(( i = i + 1 ))
done

log_must zfs create $opts -V $VOLSIZE $TESTPOOL/$TESTVOL
datasetexists $TESTPOOL/$TESTVOL || \
	log_fail "zfs create $TESTPOOL/$TESTVOL fail."
log_must zfs create -s $opts -V $VOLSIZE $TESTPOOL/$TESTVOL1
datasetexists $TESTPOOL/$TESTVOL1 || \
	log_fail "zfs create $TESTPOOL/$TESTVOL1 fail."

i=0
while (( $i < ${#RW_VOL_PROP[*]} )); do
	if [[ ${RW_VOL_PROP[$i]} != *"checksum"* ]]; then
		propertycheck $TESTPOOL/$TESTVOL ${RW_VOL_PROP[i]} || \
			log_fail "${RW_VOL_PROP[i]} is failed to set."
		propertycheck $TESTPOOL/$TESTVOL1 ${RW_VOL_PROP[i]} || \
			log_fail "${RW_VOL_PROP[i]} is failed to set."
	fi
	(( i = i + 1 ))
done

log_pass "'zfs create -o property=value -V size volume' can successfully \
	   create a ZFS volume with correct property set."
