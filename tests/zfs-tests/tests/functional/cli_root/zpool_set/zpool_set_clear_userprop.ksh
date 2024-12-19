#!/bin/ksh -p
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
