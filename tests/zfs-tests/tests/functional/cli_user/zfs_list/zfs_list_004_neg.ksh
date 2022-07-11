#!/bin/ksh -p
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
