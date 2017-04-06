#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Run the tests for the SHA-2 hash algorithm.
#

log_assert "Run the tests for the SHA-2 hash algorithm."

freq=$(get_cpu_freq)
log_must $STF_SUITE/tests/functional/checksum/sha2_test $freq

log_pass "SHA-2 tests passed."
