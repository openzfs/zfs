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
