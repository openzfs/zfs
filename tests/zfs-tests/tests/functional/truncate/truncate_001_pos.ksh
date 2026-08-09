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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/truncate/truncate.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Tests file truncation within ZFS.
#
# STRATEGY:
# 1. Open file
# 2. Write random blocks in random places
# 3. Truncate the file
# 4. Repeat steps 2 and 3 lots of times
# 5. Close the file.
#

verify_runnable "global"

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Ensure file with random blocks is truncated properly"

options=""
options_display="default options"

log_onexit cleanup

[[ -n "$TRUNC_FILESIZE" ]] && options=" $options -f $TRUNC_FILESIZE "

[[ -n "$TRUNC_BLKSIZE" ]] && options="$options -b $TRUNC_BLKSIZE "

[[ -n "$TRUNC_COUNT" ]] && options="$options -c $TRUNC_COUNT "

[[ -n "$TRUNC_SEED" ]] && options="$options -s $TRUNC_SEED "

[[ -n "$TRUNC_FILEOFFSET" ]] && options="$options -o $TRUNC_FILEOFFSET "

[[ -n "$options" ]] && options_display=$options

log_note "Invoking file_trunc with: $options_display"
log_must file_trunc $options $TESTDIR/$TESTFILE

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass "Random blocks have been truncated properly."
