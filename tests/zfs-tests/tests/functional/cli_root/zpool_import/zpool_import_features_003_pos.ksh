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
#  Pool can be imported with active read-only compatible features. If a feature
#  is read-only compatible but also inactive its property status should be
#  "inactive" rather than "readonly".
#
#  1. Create new pool.
#  2. Export and inject variety of unsupported features.
#  3. Try to import read-write, this should fail. The error should only list
#     the active read-only compatible feature and mention "readonly=on".
#  4. Import the pool in read-only mode.
#  5. Verify values of unsupported@ properties.
#
################################################################################

verify_runnable "global"

enabled_features="com.test:xxx_unsup0 com.test:xxx_unsup2"
active_features="com.test:xxx_unsup1 com.test:xxx_unsup3"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	log_must rm $VDEV0
	log_must mkfile $FILE_SIZE $VDEV0
}

log_assert "Pool with active read-only compatible features can be imported."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0
log_must zpool export $TESTPOOL1

for feature in $enabled_features $active_features; do
	log_must zhack -d $DEVICE_DIR feature enable -r $TESTPOOL1 $feature
done

for feature in $active_features; do
	log_must zhack -d $DEVICE_DIR feature ref $TESTPOOL1 $feature
done

log_mustnot zpool import -d $DEVICE_DIR $TESTPOOL1

# error message should mention "readonly"
log_must eval "zpool import -d $DEVICE_DIR $TESTPOOL1 | grep readonly"
log_mustnot poolexists $TESTPOOL1

for feature in $enabled_features; do
	log_mustnot eval "zpool import -d $DEVICE_DIR $TESTPOOL1 \
	    | grep $feature"
	log_mustnot poolexists $TESTPOOL1
done

for feature in $active_features; do
	log_must eval "zpool import -d $DEVICE_DIR $TESTPOOL1 \
	    | grep $feature"
	log_mustnot poolexists $TESTPOOL1
done

log_must zpool import -o readonly=on -d $DEVICE_DIR $TESTPOOL1

for feature in $enabled_features; do
	state=$(zpool list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "inactive" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

for feature in $active_features; do
	state=$(zpool list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "readonly" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

log_pass
