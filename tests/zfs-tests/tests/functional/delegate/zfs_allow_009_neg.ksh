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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	zfs allow can deal with invalid arguments.(Invalid options or combination)
#
# STRATEGY:
#	1. Verify invalid arguments will cause error.
#	2. Verify non-optional argument was missing will cause error.
#	3. Verify invalid options cause error.
#

verify_runnable "both"

log_assert "Verify invalid arguments are handled correctly."
log_onexit restore_root_datasets

# Permission sets are limited to 64 characters in length.
longset="set123456789012345678901234567890123456789012345678901234567890123"
for dtst in $DATASETS ; do
	log_mustnot eval "zfs allow -s @$longset $dtst"
	# Create non-existent permission set
	log_mustnot zfs allow -s @non-existent $dtst
	log_mustnot zfs allow $STAFF "atime,created,mounted" $dtst
	log_mustnot zfs allow $dtst $TESTPOOL
	log_mustnot zfs allow -c $dtst
	log_mustnot zfs allow -u $STAFF1 $dtst
	log_mustnot zfs allow -u $STAFF1 -g $STAFF_GROUP "create,destroy" $dtst
	log_mustnot zfs allow -u $STAFF1 -e "mountpoint" $dtst
done

log_pass "Invalid arguments are handled correctly."
