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
