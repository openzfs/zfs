#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# Description:
# Verify that attach/detach work while resilvering and attaching
# multiple vdevs.
#
# Strategy:
# 1. Create a single vdev pool
# 2. While healing or sequential resilvering:
#    a. Attach a vdev to convert the pool to a mirror.
#    b. Attach a vdev to convert the pool to a 3-way mirror.
#    c. Verify the original vdev cannot be removed (no redundant copies)
#    d. Detach a vdev.  Healing and sequential resilver remain running.
#    e. Detach a vdev.  Healing resilver remains running, sequential
#       resilver is canceled.
#    f. Wait for resilver to complete.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]}
}

log_assert "Verify attach/detech with multiple vdevs"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]}

# Verify resilver resumes on import.
log_must zpool create -f $TESTPOOL1 ${VDEV_FILES[0]}

for replace_mode in "healing" "sequential"; do
        #
        # Resilvers abort the dsl_scan and reconfigure it for resilvering.
        # Rebuilds cancel the dsl_scan and start the vdev_rebuild thread.
        #
        if [[ "$replace_mode" = "healing" ]]; then
                flags=""
        else
                flags="-s"
        fi

	log_mustnot is_pool_resilvering $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

	# Attach first vdev (stripe -> mirror)
	log_must zpool attach $flags $TESTPOOL1 \
	    ${VDEV_FILES[0]} ${VDEV_FILES[1]}
	log_must is_pool_resilvering $TESTPOOL1

	# Attach second vdev (2-way -> 3-way mirror)
	log_must zpool attach $flags $TESTPOOL1 \
	    ${VDEV_FILES[1]} ${VDEV_FILES[2]}
	log_must is_pool_resilvering $TESTPOOL1

	# Original vdev cannot be detached until there is sufficent redundancy.
	log_mustnot zpool detach $TESTPOOL1 ${VDEV_FILES[0]}

	# Detach first vdev (resilver keeps running)
	log_must zpool detach $TESTPOOL1 ${VDEV_FILES[1]}
	log_must is_pool_resilvering $TESTPOOL1

	#
	# Detach second vdev.  There's a difference in behavior between
	# healing and sequential resilvers.  A healing resilver will not be
	# cancelled even though there's nothing on the original vdev which
	# needs to be rebuilt.  A sequential resilver on the otherhand is
	# canceled when returning to a non-redundant striped layout.  At
	# some point the healing resilver behavior should be updated to match
	# the sequential resilver behavior.
	#
	log_must zpool detach $TESTPOOL1 ${VDEV_FILES[2]}

        if [[ "$replace_mode" = "healing" ]]; then
		log_must is_pool_resilvering $TESTPOOL1
        else
		log_mustnot is_pool_resilvering $TESTPOOL1
        fi

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait $TESTPOOL1
done

log_pass "Verify attach/detech with multiple vdevs"
