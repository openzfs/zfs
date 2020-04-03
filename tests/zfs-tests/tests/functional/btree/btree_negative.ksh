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
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Verify that the btree functions don't allow bad inputs
#
# insert_duplicate - Callers may not add values that are already in the tree
# remove_missing   - Callers may not remove values that are not in the tree
#
# Note: These invocations cause btree_test to crash, but the program disables
# core dumps first. As such, we can't use log_mustnot because it explicitly
# looks for return values that correspond to a core dump and cause a test
# failure.

btree_test -n insert_duplicate
[[ $? -eq 0 ]] && log_fail "Failure from insert_duplicate"

btree_test -n remove_missing
[[ $? -eq 0 ]] && log_fail "Failure from remove_missing"

log_pass "Btree negative tests passed"
