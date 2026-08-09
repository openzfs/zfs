#! /bin/ksh -p
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
