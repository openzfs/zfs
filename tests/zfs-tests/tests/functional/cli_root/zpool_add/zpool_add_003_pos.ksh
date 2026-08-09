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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
# 'zpool add -n <pool> <vdev> ...' can display the configuration without adding
# the specified devices to given pool
#
# STRATEGY:
# 1. Create a storage pool
# 2. Use -n to add devices to the pool
# 3. Verify the devices are not added actually
# 4. Add devices to the pool for real this time, verify the vdev tree is the
#    same printed by the dryrun iteration
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $TMPFILE_PREFIX* $VDEV_PREFIX*
}

log_assert "'zpool add -n <pool> <vdev> ...' can display the configuration" \
	"without actually adding devices to the pool."

log_onexit cleanup

typeset TMPFILE_PREFIX="$TEST_BASE_DIR/zpool_add_003"
typeset STR_DRYRUN="would update '$TESTPOOL' to the following configuration:"
typeset VDEV_PREFIX="$TEST_BASE_DIR/filedev"
typeset -a VDEV_TYPES=("" "dedup" "special" "log" "cache" "spare")

vdevs=""
config=""

# 1. Create a storage pool
log_must truncate -s $SPA_MINDEVSIZE "$VDEV_PREFIX-root"
log_must zpool create "$TESTPOOL" "$VDEV_PREFIX-root"
log_must poolexists "$TESTPOOL"
for vdevtype in "${VDEV_TYPES[@]}"; do
	log_must truncate -s $SPA_MINDEVSIZE "$VDEV_PREFIX-$vdevtype"
	vdevs="$vdevs $VDEV_PREFIX-$vdevtype"
	config="$config $vdevtype $VDEV_PREFIX-$vdevtype"
done

# 2. Use -n to add devices to the pool
log_must eval "zpool add -f -n $TESTPOOL $config > $TMPFILE_PREFIX-dryrun"
log_must grep -q "$STR_DRYRUN" "$TMPFILE_PREFIX-dryrun"

# 3. Verify the devices are not added actually
for vdev in $vdevs; do
	log_mustnot vdevs_in_pool "$TESTPOOL" "$vdev"
done

# 4. Add devices to the pool for real this time, verify the vdev tree is the
#    same printed by the dryrun iteration
log_must zpool add -f $TESTPOOL $config
zpool status $TESTPOOL | awk 'NR == 1, /NAME/ { next } /^$/ {exit}
	{print $1}' > "$TMPFILE_PREFIX-vdevtree"
awk 'NR == 1, /would/ {next}
	/^$/ {next} {print $1}' "$TMPFILE_PREFIX-dryrun" > "$TMPFILE_PREFIX-vdevtree-n"
log_must diff $TMPFILE_PREFIX-vdevtree-n $TMPFILE_PREFIX-vdevtree

log_pass "'zpool add -n <pool> <vdev> ...' executes successfully."
