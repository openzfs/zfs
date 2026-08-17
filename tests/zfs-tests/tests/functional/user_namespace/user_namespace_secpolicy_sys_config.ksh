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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/user_namespace/user_namespace_common.kshlib

#
# Verify that secpolicy_sys_config() only allows the superuser in the root/init
# namespace.
#
# Currently, zpool/zfs start by getting a list of pools, which has separate
# zone visibility checks, and then don't proceed if no pools are found. So,
# we use `zpool events` for the test, which uses secpolicy_sys_config() but
# doesn't need a name to work, and so zpool doesn't bother with the lookup.
#

verify_runnable "both"

log_assert "secpolicy_sys_config correctly limits access."

# default superuser can access pool events
log_must eval 'zpool events >/dev/null'

# regular user cannot access pool events
log_mustnot eval 'sudo -u nobody zpool events >/dev/null'

# superuser in a new user namespace cannot access pool events
log_mustnot eval 'unshare -Ur zpool events >/dev/null'

# regular user in a new user namespace cannot access pool events
log_mustnot eval 'unshare -U zpool events >/dev/null'

log_pass "secpolicy_sys_config correctly limits access."
