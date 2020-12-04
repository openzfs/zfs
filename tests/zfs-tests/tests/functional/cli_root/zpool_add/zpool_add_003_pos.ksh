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
cat "$TMPFILE_PREFIX-dryrun" | awk 'NR == 1, /would/ {next}
	/^$/ {next} {print $1}' > "$TMPFILE_PREFIX-vdevtree-n"
log_must eval "diff $TMPFILE_PREFIX-vdevtree-n $TMPFILE_PREFIX-vdevtree"

log_pass "'zpool add -n <pool> <vdev> ...' executes successfully."
