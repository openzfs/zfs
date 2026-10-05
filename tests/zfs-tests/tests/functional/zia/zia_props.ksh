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
#	Z.I.A. zpool settings work
#
# STRATEGY:
#   1. Turn on all offloads
#   2. Run zpool get on each property
#

log_must offload_all

log_must zpool get zia_available  "${TESTPOOL}"
log_must zpool get zia_provider   "${TESTPOOL}"
log_must zpool get zia_compress   "${TESTPOOL}"
log_must zpool get zia_checksum   "${TESTPOOL}"
log_must zpool get zia_raidz1_gen "${TESTPOOL}"
log_must zpool get zia_raidz2_gen "${TESTPOOL}"
log_must zpool get zia_raidz3_gen "${TESTPOOL}"
log_must zpool get zia_raidz1_rec "${TESTPOOL}"
log_must zpool get zia_raidz2_rec "${TESTPOOL}"
log_must zpool get zia_raidz3_rec "${TESTPOOL}"
log_must zpool get zia_disk_write "${TESTPOOL}"
log_must zpool get zia_file_write "${TESTPOOL}"

log_pass
