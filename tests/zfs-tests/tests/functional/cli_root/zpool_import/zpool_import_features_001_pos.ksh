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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#################################################################################
#
#  Pool can be imported with inactive unsupported features.
#
#  1. Create new pool.
#  2. Export and inject unsuppored features with zhack.
#  3. Import pool normally with no problems.
#  4. Verify that unsupported@ properties exist for the unsupported features.
#
################################################################################

verify_runnable "global"

features="com.test:xxx_unsup0 com.test:xxx_unsup1 com.test:xxx_unsup2"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	log_must rm $VDEV0
	log_must mkfile $FILE_SIZE $VDEV0
}

log_assert "Pool with inactive unsupported features can be imported."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0
log_must zpool export $TESTPOOL1

for feature in $features; do
	log_must zhack -d $DEVICE_DIR feature enable $TESTPOOL1 $feature
done

log_must zpool import -d $DEVICE_DIR $TESTPOOL1
for feature in $features; do
	state=$(zpool list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "inactive" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

log_pass
