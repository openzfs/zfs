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
