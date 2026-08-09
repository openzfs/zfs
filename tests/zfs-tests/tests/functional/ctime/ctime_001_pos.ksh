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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
# . $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
#
# Verify [acm]time is modified appropriately with xattr=on|sa

set -A args "sa" "on"

log_note "Verify [acm]time is modified appropriately."

for arg in ${args[*]}; do
	log_note "Testing with xattr set to $arg"
	log_must zfs set xattr=$arg $TESTPOOL
	log_must ctime
done

log_pass "PASS"
