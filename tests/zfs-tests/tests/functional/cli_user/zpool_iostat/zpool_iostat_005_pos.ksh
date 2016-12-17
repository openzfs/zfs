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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

#
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
#


. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

typeset testpool
if is_global_zone ; then
	testpool=$TESTPOOL
else
	testpool=${TESTPOOL%%/*}
fi

#
# DESCRIPTION:
# Verify 'zpool iostat -c CMD' works, and that VDEV_PATH and VDEV_UPATH get set.
#
# STRATEGY:
# grep for '^\s+/' to just get the vdevs (not pools).  All vdevs will start with
# a '/' when we specify the path (-P) flag. We check for "{}" to see if one
# of the VDEV variables isn't set.
#
C1=$($ZPOOL iostat -Pv $testpool | $GREP -E '^\s+/' | $WC -l)
C2=$($ZPOOL iostat -Pv -c 'echo vdev_test{$VDEV_PATH}{$VDEV_UPATH}' $testpool \
    | $GREP -E '^\s+/' | $GREP -v '{}' | $WC -l)
if [ "$C1" != "$C2" ] ; then
	log_fail "zpool iostat -c failed, expected $C1 vdevs, got $C2"
else
	log_note "zpool iostat -c passed, expected $C1 vdevs, got $C2"
fi

# Call iostat on only a specific vdev, and verify that the command only gets
# run on the vdev.  We write the command results to a temp file to verify that
# the command actually gets run, rather than just verifying that the results
# are *displayed* for the specific vdev.
TMP=$($MKTEMP)
FIRST_VDEV=$($ZPOOL iostat -Pv $testpool | $GREP -Eo '^\s+/[^ ]+' | $HEAD -n 1)
log_must $ZPOOL iostat -Pv -c "echo \$VDEV_PATH >> $TMP" $testpool \
    $FIRST_VDEV > /dev/null
C2=$($WC -w < $TMP)
$RM $TMP
if [ "$C2" != "1" ] ; then
	log_fail "zpool iostat -c <VDEV> failed, expected 1 vdev, got $C2"
else
	log_note "zpool iostat -c <VDEV> passed, expected 1 vdev, got $C2"
fi
