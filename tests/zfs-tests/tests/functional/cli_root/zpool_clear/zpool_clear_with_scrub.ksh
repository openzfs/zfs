#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025 Klara Inc.
#
# This software was developed by Mariusz Zaborski <oshogbo@FreeBSD.org>
# under sponsorship from Wasabi Technology, Inc. and Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
#	Verify that `zpool clean -s` clears any errors from the pool
#	and scrubs only recent data.`
#
# STRATEGY:
#	 1. Adjust the scrub_recent_time to a small value to make the test quick
#	    and concise.
#	 2. Create the first file.
#	 3. Export, wait, and re-import the pool to record entries in the TXG
#	    database and to add some time margin between the files.
#	 4. Create the second file.
#	 5. Inject errors into the first file.
#	 6. Run a scrub to detect the errors to the pool.
#	 7. Verify that errors in the first file are detected and that
#	    the second file is not corrupted.
#	 8. Inject errors into the second file.
#	 9. Run `zpool clear -s`.
#	10. Confirm that errors from the first file are cleared from the error
#           list, and only new errors corresponding to recent data
#           (the second file) are detected.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	rm -f $TESTDIR/*_file
	log_must restore_tunable SCRUB_RECENT_TIME
}

log_onexit cleanup

log_assert "Verify cleanup of errors and scrubbing of recent data."

log_must save_tunable SCRUB_RECENT_TIME
log_must set_tunable64 SCRUB_RECENT_TIME 30

log_must file_write -o create -f"$TESTDIR/0_file" \
    -b 512 -c 2048 -dR
log_must zpool export $TESTPOOL
log_must sleep 60
log_must zpool import $TESTPOOL
log_must file_write -o create -f"$TESTDIR/1_file" \
    -b 512 -c 2048 -dR

log_must zinject -t data -e checksum -f 100 $TESTDIR/0_file
log_must zpool scrub $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep '0_file'"
log_mustnot eval "zpool status -v $TESTPOOL | grep '1_file'"

log_must zinject -t data -e checksum -f 100 $TESTDIR/1_file
log_must zpool clear -s $TESTPOOL
log_mustnot eval "zpool status -v $TESTPOOL | grep '0_file'"
log_must eval "zpool status -v $TESTPOOL | grep '1_file'"

log_pass "Verified cleanup of errors and scrubbing of recent data."
