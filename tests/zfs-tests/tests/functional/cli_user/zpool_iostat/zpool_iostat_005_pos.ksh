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
