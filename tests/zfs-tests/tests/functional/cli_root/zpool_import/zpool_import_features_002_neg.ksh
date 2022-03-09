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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#################################################################################
#
#  Pool cannot be opened with active unsupported features. Error message should
#  list active unsupported features.
#
#  1. Create new pool.
#  2. Export and inject unsuppored features with zhack, make some of them active.
#  3. Try to import pool, error should only list active features. It should
#     not say anything about being able to import the pool in readonly mode.
#
################################################################################

verify_runnable "global"

enabled_features="com.test:xxx_unsup1 com.test:xxx_unsup3"
active_features="com.test:xxx_unsup0 com.test:xxx_unsup2"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	log_must rm $VDEV0
	log_must mkfile $FILE_SIZE $VDEV0
}

log_assert "Pool with active unsupported features cannot be imported."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0
log_must zpool export $TESTPOOL1

for feature in $enabled_features $active_features; do
	log_must zhack -d $DEVICE_DIR feature enable $TESTPOOL1 $feature
done

for feature in $active_features; do
	log_must zhack -d $DEVICE_DIR feature ref $TESTPOOL1 $feature
done

log_mustnot zpool import -d $DEVICE_DIR $TESTPOOL1

# error message should not mention "readonly"
log_mustnot eval "zpool import -d $DEVICE_DIR $TESTPOOL1 | grep -q readonly"
log_mustnot poolexists $TESTPOOL1

for feature in $active_features; do
	log_must eval "zpool import -d $DEVICE_DIR $TESTPOOL1 \
	    | grep -q $feature"
	log_mustnot poolexists $TESTPOOL1
done

for feature in $enabled_features; do
	log_mustnot eval "zpool import -d $DEVICE_DIR $TESTPOOL1 \
	    | grep -q $feature"
	log_mustnot poolexists $TESTPOOL1
done

log_pass
