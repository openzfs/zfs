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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Malformed zpool get commands are rejected
#
# STRATEGY:
#
# 1. Run several different "zpool get" commands that should fail.
#

log_assert "Malformed zpool get commands are rejected"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

set -A arguments "$TESTPOOL $TESTPOOL" "$TESTPOOL rubbish" "-v $TESTPOOL" \
		"nosuchproperty $TESTPOOL" "--$TESTPOOL" "all all" \
		"type $TESTPOOL" "usage: $TESTPOOL" "bootfs $TESTPOOL@" \
		"bootfs,bootfs $TESTPOOL" "name $TESTPOOL" "t%d%s" \
		"bootfs,delegation $TESTPOOL" "delegation $TESTPOOL@" \
		"-o name=getsubopt allocated $TESTPOOL"

for arg in $arguments
do
	log_mustnot zpool get $arg
done

log_pass "Malformed zpool get commands are rejected"
