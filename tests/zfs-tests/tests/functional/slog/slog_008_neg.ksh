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

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	A raidz/raidz2 log is not supported.
#
# STRATEGY:
#	1. Try to create pool with unsupported type
#	2. Verify failed to create pool.
#

verify_runnable "global"

log_assert "A raidz/raidz2 log is not supported."
log_onexit cleanup
log_must setup

for type in "" "mirror" "raidz" "raidz2"
do
	for spare in "" "spare"
	do
		for logtype in "raidz" "raidz1" "raidz2"
		do
			log_mustnot zpool create $TESTPOOL $type $VDEV \
				$spare $SDEV log $logtype $LDEV $LDEV2
			ldev=$(random_get $LDEV $LDEV2)
			log_mustnot verify_slog_device \
				$TESTPOOL $ldev 'ONLINE' $logtype
			log_must datasetnonexists $TESTPOOL
		done
	done
done

log_pass "A raidz/raidz2 log is not supported."
