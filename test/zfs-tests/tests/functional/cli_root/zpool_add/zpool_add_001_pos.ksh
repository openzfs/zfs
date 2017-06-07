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
        pooldevs="${disk}s${SLICE0} \
                  /dev/dsk/${disk}s${SLICE0} \
                  \"${disk}s${SLICE0} ${disk}s${SLICE1}\""
        mirrordevs="\"/dev/dsk/${disk}s${SLICE0} ${disk}s${SLICE1}\""
        raidzdevs="\"/dev/dsk/${disk}s${SLICE0} ${disk}s${SLICE1}\""

        ;;
2|*)
        pooldevs="${DISK0}s${SLICE0}\
                 \"/dev/dsk/${DISK0}s${SLICE0} ${DISK1}s${SLICE0}\" \
                 \"${DISK0}s${SLICE0} ${DISK0}s${SLICE1} ${DISK1}s${SLICE1}\"\
                 \"${DISK0}s${SLICE0} ${DISK1}s${SLICE0} ${DISK0}s${SLICE1}\
                   ${DISK1}s${SLICE1}\""
        mirrordevs="\"/dev/dsk/${DISK0}s${SLICE0} ${DISK1}s${SLICE0}\""
        raidzdevs="\"/dev/dsk/${DISK0}s${SLICE0} ${DISK1}s${SLICE0}\""

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
			create_pool "$TESTPOOL" "${disk}s${SLICE6}"
			log_must poolexists "$TESTPOOL"
			log_must $ZPOOL add -f "$TESTPOOL" ${keywords[i]} $vdev
			log_must iscontained "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        mirror)
		for vdev in "${mirrorarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"${disk}s${SLICE4}" "${disk}s${SLICE5}"
			log_must poolexists "$TESTPOOL"
			log_must $ZPOOL add "$TESTPOOL" ${keywords[i]} $vdev
			log_must iscontained "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        raidz|raidz1)
		for vdev in "${raidzarray[@]}"; do
			create_pool "$TESTPOOL" "${keywords[i]}" \
				"${disk}s${SLICE4}" "${disk}s${SLICE5}"
			log_must poolexists "$TESTPOOL"
			log_must $ZPOOL add "$TESTPOOL" ${keywords[i]} $vdev
			log_must iscontained "$TESTPOOL" "$vdev"
			destroy_pool "$TESTPOOL"
		done

		;;
        esac

        (( i = i+1 ))
done

log_pass "'zpool add <pool> <vdev> ...' executes successfully"
