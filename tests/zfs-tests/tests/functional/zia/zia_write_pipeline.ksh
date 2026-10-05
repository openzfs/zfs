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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zia/zia.kshlib

#
# DESCRIPTION:
#	Z.I.A. Write Pipeline works
#
# STRATEGY:
#   1. Turn each of the offloaded stages on and off
#       1.1. Write data to the zpool
#       1.2. Delete the file
#   2. Disable the provider for the pool and unload the provider
#   3. Do 1. again, but without a provider to make sure Z.I.A. falls back to ZFS properly
#

log_must loop_offloads_and_write
log_must unload_provider
log_must loop_offloads_and_write
log_must load_provider

log_pass
