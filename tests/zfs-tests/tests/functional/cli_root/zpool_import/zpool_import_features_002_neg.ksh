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
