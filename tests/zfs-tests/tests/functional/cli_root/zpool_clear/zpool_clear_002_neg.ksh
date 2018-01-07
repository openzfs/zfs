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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
# A badly formed parameter passed to 'zpool clear' should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad 'zpool clear' parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && \
		log_must zpool destroy -f $TESTPOOL1
	[[ -e $file ]] && \
		log_must rm -f $file
}

log_assert "Execute 'zpool clear' using invalid parameters."
log_onexit cleanup

# Create another pool for negative testing, which clears pool error
# with vdev device not in the pool vdev devices.
file=$TESTDIR/file.$$
log_must mkfile $FILESIZE $file
log_must zpool create $TESTPOOL1 $file

set -A args "" "-?" "--%" "-1234567" "0.0001" "0.7644" "-0.7644" \
		"blah" "blah $DISK" "$TESTPOOL c0txdx" "$TESTPOOL $file" \
		"$TESTPOOL c0txdx blah" "$TESTPOOL $file blah"

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zpool clear ${args[i]}

	((i = i + 1))
done

log_pass "Invalid parameters to 'zpool clear' fail as expected."
