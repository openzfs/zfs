#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Ensure znode generation number is accessible.
#
# STRATEGY:
#	1) Create a file
#	2) Verify that the znode generation number can be obtained
#	3) Verify that the znode generation number is not empty
#

verify_runnable "both"

function cleanup
{
	rm -f ${TESTFILE}
}

log_onexit cleanup

log_assert "Ensure znode generation number is accessible."

TESTFILE=${TESTDIR}/${TESTFILE0}

log_must touch ${TESTFILE}
log_must stat_generation ${TESTFILE}
log_must test $(stat_generation ${TESTFILE}) -ne 0

log_pass "Successfully obtained file znode generation number."
