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
        datasetexists $TESTPOOL/$TESTVOL && \
                log_must zfs destroy -f $TESTPOOL/$TESTVOL
	datasetexists $TESTPOOL/$TESTVOL1 && \
		log_must zfs destroy -f $TESTPOOL/$TESTVOL1
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
