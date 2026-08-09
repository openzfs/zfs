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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

#
# Copyright (c) 2016-2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
# Verify zpool iostat command mode (-c) works for all pre-baked scripts.
#
# STRATEGY:
# 1. Make sure each script creates at least one new column.
# 2. Make sure the new column values exist.
# 3. Make sure we can run multiple scripts in one -c line

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

typeset testpool
if is_global_zone ; then
	testpool=$TESTPOOL
else
	testpool=${TESTPOOL%%/*}
fi

files="$(ls $ZPOOL_SCRIPT_DIR)"
scripts=""
for i in $files ; do
	if [ ! -x "$ZPOOL_SCRIPT_DIR/$i" ] ; then
		# Skip non-executables
		continue
	fi

	# Collect executable script names
	scripts="$scripts $i"

	# Run each one with -c
	test_zpool_script "$i" "$testpool" "zpool iostat -Pv -c"
done

# Test that we can run multiple scripts separated with a comma by running
# all the scripts in a single -c line.
allscripts="$(echo $scripts | sed -r 's/[[:blank:]]+/,/g')"
test_zpool_script "$allscripts" "$testpool" "zpool iostat -Pv -c"

exit 0
