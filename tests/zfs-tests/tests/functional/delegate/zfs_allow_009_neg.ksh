#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
