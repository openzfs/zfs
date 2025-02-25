#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib

log_assert "ICP passes test vectors for AES-CCM"

log_must crypto_test -c $STF_SUITE/tests/functional/crypto/aes_ccm_test.txt

log_pass "ICP passes test vectors for AES-CCM"
