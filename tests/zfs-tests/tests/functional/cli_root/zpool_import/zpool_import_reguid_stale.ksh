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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# After 'zpool reguid', a stale copy of one of the pool's devices (same
# vdev guid, old pool guid) must be listed and imported as the old pool,
# not matched to the new pool's device (openzfs/zfs#16989).
#
# STRATEGY:
# 1. Create a pool on a file vdev, export it and copy the file aside.
# 2. Import the pool, reguid it, write a marker, export it.
# 3. Put the copy next to the original.
# 4. 'zpool import -d' must list two pools with different ids, each on
#    its own device.
# 5. Importing the old id must give the old pool, with the old marker.
#

verify_runnable "global"

typeset dir=$TEST_BASE_DIR/import_reguid_stale
typeset pool=reguid_stale

function cleanup
{
	poolexists $pool && destroy_pool $pool
	poolexists ${pool}_old && destroy_pool ${pool}_old
	rm -rf $dir
}

log_onexit cleanup

log_assert "A stale device copy of a reguided pool is listed as the old pool"

log_must mkdir -p $dir/aside
log_must truncate -s $MINVDEVSIZE $dir/orig
log_must zpool create $pool $dir/orig
log_must eval "echo old > /$pool/marker"
typeset old=$(get_pool_prop guid $pool)
log_must zpool export $pool
log_must cp $dir/orig $dir/aside/copy

log_must zpool import -d $dir $pool
log_must zpool reguid $pool
log_must eval "echo new > /$pool/marker"
typeset new=$(get_pool_prop guid $pool)
log_must zpool export $pool
log_must mv $dir/aside/copy $dir/copy

typeset listing=$(zpool import -d $dir)
log_note "$listing"
echo "$listing" | grep -q "id: $old" || \
    log_fail "the old pool $old is not listed"
echo "$listing" | grep -q "id: $new" || \
    log_fail "the new pool $new is not listed"
[[ $(echo "$listing" | grep -c "$dir/copy") -eq 1 ]] || \
    log_fail "the copy is not listed exactly once"

log_must zpool import -d $dir $old ${pool}_old
[[ "$(get_pool_prop guid ${pool}_old)" == "$old" ]] || \
    log_fail "importing $old gave a different pool"
[[ "$(cat /${pool}_old/marker)" == "old" ]] || \
    log_fail "importing $old gave the new pool's data"

log_pass "A stale device copy of a reguided pool is listed as the old pool"
