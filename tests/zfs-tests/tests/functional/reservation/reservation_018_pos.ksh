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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# Verify that reservation doesn't inherit its value from parent.
#
# STRATEGY:
# 1) Create a filesystem tree
# 2) Set reservation for parents
# 3) Verify that the 'reservation' for descendent doesnot inherit the value.
#

verify_runnable "both"

function cleanup
{
	datasetexists $fs_child && destroy_dataset $fs_child
	log_must zfs set reservation=$reserv_val $fs
}

log_onexit cleanup

log_assert "Verify that reservation doesnot inherit its value from parent."

fs=$TESTPOOL/$TESTFS
fs_child=$TESTPOOL/$TESTFS/$TESTFS

space_avail=$(get_prop available $fs)
reserv_val=$(get_prop reservation $fs)
typeset reservsize=$space_avail
((reservsize = reservsize / 2))
log_must zfs set reservation=$reservsize $fs

log_must zfs create $fs_child
rsv_space=$(get_prop reservation $fs_child)
[[ $rsv_space == $reservsize ]] && \
    log_fail "The reservation of child dataset inherits its value from parent."

log_pass "reservation doesnot inherit its value from parent as expected."
