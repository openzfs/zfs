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

. $STF_SUITE/tests/functional/cli_user/zfs_list/zfs_list.kshlib

#
# DESCRIPTION:
# 	Verify 'zfs list [-r]' should fail while
#		* the given dataset does not exist
#		* the given path does not exist.
#		* the given path does not belong to zfs.
#
# STRATEGY:
# 1. Create an array of invalid options.
# 2. Execute each element in the array.
# 3. Verify failure is returned.
#

verify_runnable "both"

log_assert "Verify 'zfs list [-r]' should fail while the given " \
	"dataset/path does not exist or not belong to zfs."

paths="$TESTPOOL/NONEXISTFS $TESTPOOL/$TESTFS/NONEXISTFS \
	/$TESTDIR/NONEXISTFS /devices /tmp ./../devices ./../tmp"

cd /tmp

for fs in $paths ; do
    # In cases when ZFS is on root, /tmp will belong to ZFS and hence must be
    # skipped
    if ! is_fs_type_zfs $fs; then
        log_mustnot zfs list $fs
        log_mustnot zfs list -r $fs
    fi
done

log_pass "'zfs list [-r]' fails while the given dataset/path does not exist " \
	"or not belong to zfs."
