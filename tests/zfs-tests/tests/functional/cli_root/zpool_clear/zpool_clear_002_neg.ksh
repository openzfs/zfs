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
