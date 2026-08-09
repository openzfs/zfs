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
#	"zfs snapshot -r" fails with invalid arguments or scenarios.
#	The invalid scenarios may include:
#	(1) The child filesystem already has snapshot with the same name
#	(2) The child volume already has snapshot with the same name
#
# STRATEGY:
#	1. Create an array of invalid arguments
#	2. Execute 'zfs snapshot -r' with each argument in the array,
#	3. Verify an error is returned.
#

verify_runnable "both"

function cleanup
{
	typeset snap

	for snap in $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP \
		$TESTPOOL/$TESTCTR/$TESTVOL@$TESTSNAP;
	do
		snapexists $snap && destroy_dataset $snap
	done

	datasetexists $TESTPOOL/$TESTCTR/$TESTVOL && \
		destroy_dataset $TESTPOOL/$TESTCTR/$TESTVOL -rf

}

log_assert "'zfs snapshot -r' fails with invalid arguments or scenarios. "
log_onexit cleanup

set -A args "" \
    "$TESTPOOL/$TESTCTR@$TESTSNAP" "$TESTPOOL/$TESTCTR@blah?" \
    "$TESTPOOL/$TESTCTR@blah*" "@$TESTSNAP" "$TESTPOOL/$TESTCTR@" \
    "$TESTPOOL/$TESTFS/$TESTSNAP" "blah/blah@$TESTSNAP" \
    "$TESTPOOL/$TESTCTR@$TESTSNAP@$TESTSNAP"

# setup preparations
log_must zfs snapshot $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP

# testing
typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zfs snapshot -r ${args[i]}

	((i = i + 1))
done

# Testing the invalid scenario: the child volume already has an
# identical name snapshot, zfs snapshot -r should fail when
# creating snapshot with -r for the parent
log_must zfs destroy $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTCTR/$TESTVOL
else
	log_must zfs create $TESTPOOL/$TESTCTR/$TESTVOL
fi
log_must zfs snapshot $TESTPOOL/$TESTCTR/$TESTVOL@$TESTSNAP

log_mustnot zfs snapshot -r $TESTPOOL/$TESTCTR@$TESTSNAP

log_pass "'zfs snapshot -r' fails with invalid arguments or scenarios as expected."
