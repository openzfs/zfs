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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs send -i' can deal with abbreviated snapshot name.
#
# STRATEGY:
#	1. Create pool, fs and two snapshots.
#	2. Make sure 'zfs send -i' support abbreviated snapshot name.
#

verify_runnable "both"

function cleanup
{
	datasetexists $snap1 && destroy_dataset $snap1
	datasetexists $snap2 && destroy_dataset $snap2
}

log_assert "'zfs send -i' can deal with abbreviated snapshot name."
log_onexit cleanup

snap1=$TESTPOOL/$TESTFS@snap1; snap2=$TESTPOOL/$TESTFS@snap2

set -A args "$snap1 $snap2" \
	"${snap1##*@} $snap2" "@${snap1##*@} $snap2"

log_must zfs snapshot $snap1
log_must zfs snapshot $snap2

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_must eval "zfs send -i ${args[i]} > /dev/null"

	(( i += 1 ))
done

log_pass "'zfs send -i' deal with abbreviated snapshot name passed."
