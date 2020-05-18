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
# Verify that attach/detach work while resilvering or rebuilding and
# attaching multiple vdevs.
#
# Strategy:
# 1. Create a single vdev pool
# 2. While resilvering or rebuilding:
#    a. Attach a vdev to convert the pool to a mirror.
#    b. Attach a vdev to convert the pool to a 3-way mirror.
#    c. Verify the original vdev cannot be removed (no redundant copies)
#    d. Detach a vdev.  Resilver and rebuild remain running.
#    e. Detach a vdev.  Resilver remains running, rebuild is canceled.
#    f. Wait for resilver/rebuild to complete.
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

for replace_mode in "resilver" "rebuild"; do
        #
        # Resilvers abort the dsl_scan and reconfigure it for resilvering.
        # Rebuilds cancel the dsl_scan and start the vdev_rebuild thread.
        #
        if [[ "$replace_mode" = "resilver" ]]; then
		check="is_pool_resilvering"
                flags=""
        else
		check="is_pool_rebuilding"
                flags="-r"
        fi

	log_mustnot $check $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

	# Attach first vdev (stripe -> mirror)
	log_must zpool attach $flags $TESTPOOL1 \
	    ${VDEV_FILES[0]} ${VDEV_FILES[1]}
	log_must $check $TESTPOOL1

	# Attach second vdev (2-way -> 3-way mirror)
	log_must zpool attach $flags $TESTPOOL1 \
	    ${VDEV_FILES[1]} ${VDEV_FILES[2]}
	log_must $check $TESTPOOL1

	# Original vdev cannot be detached until there is sufficent redundancy.
	log_mustnot zpool detach $TESTPOOL1 ${VDEV_FILES[0]}

	# Detach first vdev (resilver/rebuild keeps running)
	log_must zpool detach $TESTPOOL1 ${VDEV_FILES[1]}
	log_must $check $TESTPOOL1

	#
	# Detach second vdev.  There's a difference in behavior between
	# resilvers and rebuilds.  A resilver will not be cancelled even
	# though there's nothing on the original vdev which needs to be
	# rebuilt.  A rebuild on the otherhand is canceled when returning
	# to a non-redundant striped layout.  At some point the resilver
	# behavior should be updated to match the rebuild behavior.
	#
	log_must zpool detach $TESTPOOL1 ${VDEV_FILES[2]}

        if [[ "$replace_mode" = "resilver" ]]; then
		log_must $check $TESTPOOL1
        else
		log_mustnot $check $TESTPOOL1
        fi

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait $TESTPOOL1
done

log_pass "Verify attach/detech with multiple vdevs"
