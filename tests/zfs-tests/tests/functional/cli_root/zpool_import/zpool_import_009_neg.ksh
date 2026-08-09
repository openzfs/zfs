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
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

# DESCRIPTION:
#	Try each 'zpool import' with inapplicable scenarios to make sure
#	it returns an error. include:
#		* A non-existent pool name is given
#		* '-d', but no device directory specified
#		* '-R', but no alter root directory specified
#		* '-a', but a pool name specified either
#		* more than 2 pool names is given
#		* The new pool name specified already exists
#		* Contain invalid characters not allowed in the ZFS namespace
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#

verify_runnable "global"

set -A args "blah" "-d" "-R" "-a $TESTPOOL" \
	"$TESTPOOL ${TESTPOOL}-new ${TESTPOOL}-new" \
	"$TESTPOOL $TESTPOOL1" \
	"$TESTPOOL ${TESTPOOL1}*" "$TESTPOOL ${TESTPOOL1}?"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"

function cleanup
{
	typeset -i i=0
	typeset -i j=0

	while (( i < ${#pools[*]} )); do

		poolexists ${pools[i]} && \
			log_must zpool export ${pools[i]}

		datasetexists "${pools[i]}/$TESTFS" || \
			log_must zpool import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must zfs mount ${pools[i]}/$TESTFS

		((i = i + 1))
	done

	cleanup_filesystem $TESTPOOL1 $TESTFS

        destroy_pool $TESTPOOL1
}

log_onexit cleanup

log_assert "Badly-formed 'zpool import' with inapplicable scenarios " \
	"should return an error."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1

log_must zpool export $TESTPOOL

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zpool import ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zpool import' with inapplicable scenarios " \
	"fail as expected."
