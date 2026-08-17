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
# Verify that secpolicy_zinject() only allows the superuser in the root/init
# namespace.
#
# Because we don't actually want to inject anything, we just try to run
# `zinject -c all`, which with no injections is just a permission check and
# then an (effective) no-op.
#

verify_runnable "both"

log_assert "secpolicy_zinject correctly limits access."

# default superuser can access pool events
log_must zinject -c all

# regular user cannot access pool events
log_mustnot sudo -u nobody zinject -c all

# superuser in a new user namespace cannot access pool events
log_mustnot unshare -Ur zinject -c all

# regular user in a new user namespace cannot access pool events
log_mustnot unshare -U zinject -c all

log_pass "secpolicy_zinject correctly limits access."

