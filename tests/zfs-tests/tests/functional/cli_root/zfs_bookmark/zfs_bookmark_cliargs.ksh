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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs bookmark' should work with both full and short arguments.
#
# STRATEGY:
# 1. Create initial snapshot
# 2. Verify we can create a bookmark specifying snapshot and bookmark full paths
# 3. Verify we can create a bookmark specifying the snapshot name
# 4. Verify we can create a bookmark specifying the bookmark name
# 5. Verify at least a full dataset path is required and both snapshot and
#    bookmark name must be valid
#

verify_runnable "both"

function cleanup
{
	if snapexists "$DATASET@$TESTSNAP"; then
		log_must zfs destroy "$DATASET@$TESTSNAP"
	fi
	if bkmarkexists "$DATASET#$TESTBM"; then
		log_must zfs destroy "$DATASET#$TESTBM"
	fi
}

log_assert "'zfs bookmark' should work only when passed valid arguments."
log_onexit cleanup

DATASET="$TESTPOOL/$TESTFS"
TESTSNAP='snapshot'
TESTBM='bookmark'

# Create initial snapshot
log_must zfs snapshot "$DATASET@$TESTSNAP"

# Verify we can create a bookmark specifying snapshot and bookmark full paths
log_must zfs bookmark "$DATASET@$TESTSNAP" "$DATASET#$TESTBM"
log_must eval "bkmarkexists $DATASET#$TESTBM"
log_must zfs destroy "$DATASET#$TESTBM"

# Verify we can create a bookmark specifying the snapshot name
log_must zfs bookmark "@$TESTSNAP" "$DATASET#$TESTBM"
log_must eval "bkmarkexists $DATASET#$TESTBM"
log_must zfs destroy "$DATASET#$TESTBM"

# Verify we can create a bookmark specifying the bookmark name
log_must zfs bookmark "$DATASET@$TESTSNAP" "#$TESTBM"
log_must eval "bkmarkexists $DATASET#$TESTBM"
log_must zfs destroy "$DATASET#$TESTBM"

# Verify at least a full dataset path is required and both snapshot and
# bookmark name must be valid
log_mustnot zfs bookmark "@$TESTSNAP" "#$TESTBM"
log_mustnot zfs bookmark "$TESTSNAP" "#$TESTBM"
log_mustnot zfs bookmark "@$TESTSNAP" "$TESTBM"
log_mustnot zfs bookmark "$TESTSNAP" "$TESTBM"
log_mustnot zfs bookmark "$TESTSNAP" "$DATASET#$TESTBM"
log_mustnot zfs bookmark "$DATASET" "$TESTBM"
log_mustnot zfs bookmark "$DATASET@" "$TESTBM"
log_mustnot zfs bookmark "$DATASET" "#$TESTBM"
log_mustnot zfs bookmark "$DATASET@" "#$TESTBM"
log_mustnot zfs bookmark "$DATASET@$TESTSNAP" "$TESTBM"
log_mustnot zfs bookmark "@" "#$TESTBM"
log_mustnot zfs bookmark "@" "#"
log_mustnot zfs bookmark "@$TESTSNAP" "#"
log_mustnot zfs bookmark "@$TESTSNAP" "$DATASET#"
log_mustnot zfs bookmark "@$TESTSNAP" "$DATASET"
log_mustnot zfs bookmark "$TESTSNAP" "$DATASET#"
log_mustnot zfs bookmark "$TESTSNAP" "$DATASET"
log_mustnot eval "bkmarkexists $DATASET#$TESTBM"

log_pass "'zfs bookmark' works as expected only when passed valid arguments."
