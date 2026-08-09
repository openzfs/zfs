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

#
# DESCRIPTION:
#
# zpool set comment property on root vdev
#
# STRATEGY:
#	1. set a property on root vdev
#	2. verify the property is set
#

log_assert "zpool set comment property on root vdev"

log_must zpool set comment="openzfs" ${TESTPOOL} root

COMMENT="$(zpool get -H -o value comment ${TESTPOOL} root)"
if [ $? -ne 0 ]; then
    log_fail "cant retrieve comment property from root vdev"
fi

if [ "$COMMENT" != "openzfs" ]; then
    log_fail "unexpected value for comment property: $COMMENT != \"openzfs\""
fi

log_pass "zpool set comment property on root vdev"
