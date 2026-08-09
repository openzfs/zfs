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
#	'zfs rename' can address the abbreviated snapshot name.
#
# STRATEGY:
#	1. Create pool, fs and snap.
#	2. Verify 'zfs rename' support the abbreviated snapshot name.
#

verify_runnable "both"

function cleanup
{
	datasetexists $snap && destroy_dataset $snap
}

log_assert "'zfs rename' can address the abbreviated snapshot name."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; snap=$fs@snap
set -A newname "$fs@new-snap" "@new-snap" "new-snap"

log_must zfs snapshot $snap
log_must datasetexists $snap

typeset -i i=0
while ((i < ${#newname[*]} )); do
        log_must zfs rename $snap ${newname[$i]}
	log_must datasetexists ${snap%%@*}@${newname[$i]##*@}
	log_must zfs rename ${snap%%@*}@${newname[$i]##*@} $snap

	((i += 1))
done

log_pass "'zfs rename' address the abbreviated snapshot name passed."
