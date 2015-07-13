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
	destroy_pool -f $TESTPOOL1

	log_must $RM $VDEV0
	log_must $MKFILE -s $FILE_SIZE $VDEV0
}

log_assert "Pool with active read-only compatible features can be imported."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0
log_must $ZPOOL export $TESTPOOL1

for feature in $enabled_features $active_features; do
	log_must $ZHACK -d $DEVICE_DIR feature enable -r $TESTPOOL1 $feature
done

for feature in $active_features; do
	log_must $ZHACK -d $DEVICE_DIR feature ref $TESTPOOL1 $feature
done

log_mustnot $ZPOOL import -d $DEVICE_DIR $TESTPOOL1

# error message should mention "readonly"
log_must eval "$ZPOOL import -d $DEVICE_DIR $TESTPOOL1 | $GREP readonly"
log_mustnot poolexists $TESTPOOL1

for feature in $enabled_features; do
	log_mustnot eval "$ZPOOL import -d $DEVICE_DIR $TESTPOOL1 \
	    | $GREP $feature"
	log_mustnot poolexists $TESTPOOL1
done

for feature in $active_features; do
	log_must eval "$ZPOOL import -d $DEVICE_DIR $TESTPOOL1 \
	    | $GREP $feature"
	log_mustnot poolexists $TESTPOOL1
done

log_must $ZPOOL import -o readonly=on -d $DEVICE_DIR $TESTPOOL1

for feature in $enabled_features; do
	state=$($ZPOOL list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "inactive" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

for feature in $active_features; do
	state=$($ZPOOL list -Ho unsupported@$feature $TESTPOOL1)
        if [[ "$state" != "readonly" ]]; then
		log_fail "unsupported@$feature is '$state'"
        fi
done

log_pass
