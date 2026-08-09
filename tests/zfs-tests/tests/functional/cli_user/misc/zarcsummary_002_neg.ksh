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
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

is_freebsd && ! python3 -c 'import sysctl' 2>/dev/null && log_unsupported "python3 sysctl module missing"

log_assert "zarcsummary generates an error code with invalid options"

for arg in "-x" "-5" "-p 7" "--err" "-@"; do
        log_mustnot eval "zarcsummary $arg > /dev/null"
done

log_pass "zarcsummary generates an error code with invalid options"
