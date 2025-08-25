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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# The `btree_test` binary runs a series of positive tests when called
# without arguments.
#
# insert_find_remove - Basic functionality test
# find_without_index - Using the find function with a NULL argument
# drain_tree         - Fill the tree then empty it using the first and last
#                      functions
# stress_tree        - Allow the tree to have items added and removed for a
#                      given amount of time
#

log_must btree_test

log_pass "Btree positive tests passed"
