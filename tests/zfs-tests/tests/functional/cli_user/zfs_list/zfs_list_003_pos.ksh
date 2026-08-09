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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	Verify 'zfs list -r' could recursively display any children
#	of the dataset.
#
# STRATEGY:
# 1. Prepare a set of datasets by hierarchy.
# 2. Execute 'zfs list -r' at the top of these datasets.
# 3. Verify all child datasets are all be shown.
#

function cleanup
{
	if [[ -f $tmpfile ]]; then
		rm -f $tmpfile
	fi
}

verify_runnable "both"
log_onexit cleanup

log_assert "Verify 'zfs list -r' could display any children recursively."

tmpfile=$TEST_BASE_DIR/zfslist.out.$$
children="$TESTPOOL/$TESTFS"

for fs in $DATASETS ; do
	children="$children $TESTPOOL/$TESTFS/$fs"
done

cd /tmp

for path in $TESTPOOL/$TESTFS $TESTDIR ./../$TESTDIR ; do
	zfs list -rH -o name $path > $tmpfile
	for fs in $children ; do
		log_must grep -qxF "$fs" $tmpfile
	done
done

log_pass "'zfs list -r' could display any children recursively."
