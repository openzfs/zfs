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
# Copyright (c) 2012 by Delphix. All rights reserved.
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
	destroy_pool -f $TESTPOOL1

	log_must $RM $VDEV0
	log_must $MKFILE -s $FILE_SIZE $VDEV0
}

log_assert "Pool with inactive unsupported features can be imported."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0
log_must $ZPOOL export $TESTPOOL1

for feature in $features; do
	log_must $ZHACK -d $DEVICE_DIR feature enable $TESTPOOL1 $feature
done

log_must $ZPOOL import -d $DEVICE_DIR $TESTPOOL1
for feature in $features; do
	state=$($ZPOOL list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "inactive" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

log_pass
