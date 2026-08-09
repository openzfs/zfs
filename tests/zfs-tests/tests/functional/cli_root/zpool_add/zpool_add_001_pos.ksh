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
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $disk0 $disk1
}

log_assert "'zpool add <pool> <vdev> ...' can add devices to the pool."

log_onexit cleanup

set -A keywords "" "mirror" "raidz" "raidz1" "draid:1s" "draid1:1s" "spare"

pooldevs="${DISK0} \
	\"${DISK0} ${DISK1}\" \
	\"${DISK0} ${DISK1} ${DISK2}\""
mirrordevs="\"${DISK0} ${DISK1}\""
raidzdevs="\"${DISK0} ${DISK1}\""
draiddevs="\"${DISK0} ${DISK1} ${DISK2}\""

disk0=$TEST_BASE_DIR/disk0
disk1=$TEST_BASE_DIR/disk1
disk2=$TEST_BASE_DIR/disk2
truncate -s $MINVDEVSIZE $disk0 $disk1 $disk2

typeset -i i=0
typeset vdev
eval set -A poolarray $pooldevs
eval set -A mirrorarray $mirrordevs
eval set -A raidzarray $raidzdevs
eval set -A draidarray $draiddevs

while (( $i < ${#keywords[*]} )); do

        case ${keywords[i]} in
        ""|spare)
		for vdev in "${poolarray[@]}"; do
			create_pool "$TESTPOOL" "$disk0"
			log_must poolexists "$TESTPOOL"
			log_must zpool add -f "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        mirror)
		for vdev in "${mirrorarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"$disk0" "$disk1"
			log_must poolexists "$TESTPOOL"
			log_must zpool add "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        raidz|raidz1)
		for vdev in "${raidzarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"$disk0" "$disk1"
			log_must poolexists "$TESTPOOL"
			log_must zpool add "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        draid:1s|draid1:1s)
		for vdev in "${draidarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"$disk0" "$disk1" "$disk2"
			log_must poolexists "$TESTPOOL"
			log_must zpool add "$TESTPOOL" ${keywords[i]} $vdev
			log_must vdevs_in_pool "$TESTPOOL" "$vdev"
			log_must vdevs_in_pool "$TESTPOOL" "draid1-0-0"
			log_must vdevs_in_pool "$TESTPOOL" "draid1-1-0"
			destroy_pool "$TESTPOOL"
		done

		;;
        esac

        (( i = i+1 ))
done

log_pass "'zpool add <pool> <vdev> ...' executes successfully"
