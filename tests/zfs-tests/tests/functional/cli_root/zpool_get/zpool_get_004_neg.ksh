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
