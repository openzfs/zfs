#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify 'zpool history' can deal with non-existent pools and garbage
#	to the command.
#
# STRATEGY:
#	1. Create pool, volume & snap
#	2. Verify 'zpool history' can cope with incorrect arguments.
#

verify_runnable "global"

snap=$TESTPOOL/$TESTFS@snap
clone=$TESTPOOL/clone

set -A neg_opt "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL" "-t $TESTPOOL" \
	"-v $TESTPOOL" "$snap" "$clone" "nonexist" "TESTPOOL"

function cleanup
{
	datasetexists $clone && destroy_dataset $clone
	datasetexists $snap && destroy_dataset $snap
}

log_assert "Verify 'zpool history' can deal with non-existent pools and " \
	"garbage to the command."
log_onexit cleanup

log_must zfs snapshot $snap
log_must zfs clone $snap $clone

for opt in "${neg_opt[@]}"; do
	log_mustnot eval "zpool history $opt > /dev/null"
done

log_pass "'zpool history' command line negation test passed."
