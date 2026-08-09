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
# Copyright (c) 2024, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_set/zpool_set_common.kshlib

verify_runnable "both"

log_assert "Setting a user-defined property to the empty string removes it."
log_onexit cleanup_user_prop $TESTPOOL

log_must zpool set cool:pool=hello $TESTPOOL
log_must check_user_prop $TESTPOOL cool:pool hello local
log_must zpool set cool:pool= $TESTPOOL
log_must check_user_prop $TESTPOOL cool:pool '-' default

log_must zpool set cool:vdev=goodbye $TESTPOOL root
log_must check_vdev_user_prop $TESTPOOL root cool:vdev goodbye local
log_must zpool set cool:vdev= $TESTPOOL root
log_must check_vdev_user_prop $TESTPOOL root cool:vdev '-' default

log_pass "Setting a user-defined property to the empty string removes it."
