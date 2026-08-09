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
# Copyright (c) 2023, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

if ! command -v clonefile > /dev/null ; then
  log_unsupported "clonefile program required to test block cloning"
fi
if ! command -v clone_mmap_cached > /dev/null ; then
  log_unsupported "clone_mmap_cached program required to test block cloning"
fi

verify_runnable "global"

if tunable_exists BCLONE_ENABLED ; then
    log_must save_tunable BCLONE_ENABLED
    log_must set_tunable32 BCLONE_ENABLED 1
fi

log_pass
