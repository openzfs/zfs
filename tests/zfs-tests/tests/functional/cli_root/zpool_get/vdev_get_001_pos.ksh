#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2022, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_get/vdev_get.cfg

#
# DESCRIPTION:
#
# zpool get <pool> root works as expected
#
# STRATEGY:
#
# 1. use zpool get to retrieve properties from root vdev
# 2. verify expected properties match detected properties
#

log_assert "zpool get all on root vdev"

EXPECT="$(zpool get -H all ${TESTPOOL} root | wc -l)"
if [ $? -ne 0 ]; then
    log_fail "cannot retrieve properties from root vdev"
fi

i=0;
while [ $i -lt "${#properties[@]}" ]
do
	log_must zpool get -H "${properties[$i]}" "$TESTPOOL" root
	i=$(($i+1))
done

EXPECT=$((EXPECT))
if [ $i -gt $EXPECT ]; then
	log_fail "found vdev properties not in vdev_get.cfg: $i/$EXPECT."
elif [ $i -lt $EXPECT ]; then
    log_fail "expected properties not found in vdev_get.cfg: $i/$EXPECT."
fi

log_pass "zpool get all on root vdev"
