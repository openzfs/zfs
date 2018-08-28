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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#	'zpool add <pool> <vdev> ...' can successfully add the specified
# devices to the given pool
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add spare devices to the pool
#	3. Verify the devices are added to the pool successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

log_assert "'zpool add <pool> <vdev> ...' can add devices to the pool."

log_onexit cleanup

set -A keywords "" "mirror" "raidz" "raidz1" "spare"

case $DISK_ARRAY_NUM in
0|1)
	pooldevs="${disk}${SLICE_PREFIX}${SLICE0} \
		${DEV_DSKDIR}/${disk}${SLICE_PREFIX}${SLICE0} \
		\"${disk}${SLICE_PREFIX}${SLICE0} \
		${disk}${SLICE_PREFIX}${SLICE1}\""
	mirrordevs="\"${DEV_DSKDIR}/${disk}${SLICE_PREFIX}${SLICE0} \
		${disk}${SLICE_PREFIX}${SLICE1}\""
	raidzdevs="\"${DEV_DSKDIR}/${disk}${SLICE_PREFIX}${SLICE0} \
		${disk}${SLICE_PREFIX}${SLICE1}\""

	;;
2|*)
	pooldevs="${DISK0}${SLICE_PREFIX}${SLICE0} \
		\"${DEV_DSKDIR}/${DISK0}${SLICE_PREFIX}${SLICE0} \
		${DISK1}${SLICE_PREFIX}${SLICE0}\" \
		\"${DISK0}${SLICE_PREFIX}${SLICE0} \
		${DISK0}${SLICE_PREFIX}${SLICE1} \
		${DISK1}${SLICE_PREFIX}${SLICE1}\"\
		\"${DISK0}${SLICE_PREFIX}${SLICE0} \
		${DISK1}${SLICE_PREFIX}${SLICE0} \
		${DISK0}${SLICE_PREFIX}${SLICE1}\
		${DISK1}${SLICE_PREFIX}${SLICE1}\""
	mirrordevs="\"${DEV_DSKDIR}/${DISK0}${SLICE_PREFIX}${SLICE0} \
		${DISK1}${SLICE_PREFIX}${SLICE0}\""
	raidzdevs="\"${DEV_DSKDIR}/${DISK0}${SLICE_PREFIX}${SLICE0} \
		${DISK1}${SLICE_PREFIX}${SLICE0}\""

	;;
esac

typeset -i i=0
typeset vdev
eval set -A poolarray $pooldevs
eval set -A mirrorarray $mirrordevs
eval set -A raidzarray $raidzdevs

while (( $i < ${#keywords[*]} )); do

        case ${keywords[i]} in
        ""|spare)
		for vdev in "${poolarray[@]}"; do
			create_pool "$TESTPOOL" "${disk}${SLICE_PREFIX}${SLICE6}"
			log_must poolexists "$TESTPOOL"
			log_must zpool add -f "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        mirror)
		for vdev in "${mirrorarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"${disk}${SLICE_PREFIX}${SLICE4}" \
				"${disk}${SLICE_PREFIX}${SLICE5}"
			log_must poolexists "$TESTPOOL"
			log_must zpool add "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        raidz|raidz1)
		for vdev in "${raidzarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"${disk}${SLICE_PREFIX}${SLICE4}" \
				"${disk}${SLICE_PREFIX}${SLICE5}"
			log_must poolexists "$TESTPOOL"
			log_must zpool add "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        esac

        (( i = i+1 ))
done

log_pass "'zpool add <pool> <vdev> ...' executes successfully"
