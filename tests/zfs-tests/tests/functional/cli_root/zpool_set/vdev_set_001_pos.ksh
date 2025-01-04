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
