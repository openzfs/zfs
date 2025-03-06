#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
