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
