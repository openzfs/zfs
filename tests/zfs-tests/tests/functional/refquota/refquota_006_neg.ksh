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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs set refquota/refreserv' can handle incorrect arguments correctly.
#
# STRATEGY:
#	1. Setup incorrect arguments arrays.
#	2. Set the bad argument to refquota.
#	3. Verify zfs can handle it correctly.
#

verify_runnable "both"

function cleanup
{
	log_must zfs set refquota=none $TESTPOOL/$TESTFS
	log_must zfs set refreserv=none $TESTPOOL/$TESTFS
}

log_assert "'zfs set refquota' can handle incorrect arguments correctly."
log_onexit cleanup

set -A badopt	\
	"None"		"-1"		"1TT"		"%5"		\
	"123!"		"@456"		"7#89" 		"0\$"		\
	"abc123%"	"123%s"		"12%s3"		"%c123"		\
	"123%d"		"%x123"		"12%p3" 	"^def456" 	\
	"\0"		"x0"

typeset -i i=0
while ((i < ${#badopt[@]})); do
	log_mustnot zfs set refquota=${badopt[$i]} $TESTPOOL/$TESTFS
	log_mustnot zfs set refreserv=${badopt[$i]} $TESTPOOL/$TESTFS

	((i += 1))
done

log_pass "'zfs set refquota' can handle incorrect arguments correctly."
