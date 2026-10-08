#!/bin/ksh
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
# Description:
# Verify that a pool whose MOS has blocks with large (dynamic) gang headers
# can be imported (openzfs/zfs#19189).
#
# Strategy:
# 1. Create an ashift=12 pool from file vdevs with long, random path names,
#    so the pool's packed config (a MOS object the import reads before
#    feature refcounts are loaded) is several KB even when compressed.
# 2. Force ganging of every block bigger than a 4K gang header, and write
#    data, so large gang headers are used and dynamic_gang_header goes
#    active.
# 3. Add another vdev, which rewrites the config as a gang block with a
#    large header.
# 4. Export and import the pool, and scrub it.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "A pool with large gang headers in its MOS can be imported"

preamble
log_onexit cleanup_long_vdevs

typeset vdir=$TEST_BASE_DIR/dyn_header_import

function cleanup_long_vdevs
{
	cleanup
	rm -rf $vdir
}

# Each file vdev gets its own directories with long random names, so the
# paths in the config don't compress (alphanumeric noise has no repeats).
function long_path
{
	typeset d=$vdir
	for c in 1 2 3; do
		d=$d/$(tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 200)
	done
	echo $d
}
typeset -a vdevs
typeset -a dirs
for i in $(seq 1 9); do
	dirs[$i]=$(long_path)
	log_must mkdir -p ${dirs[$i]}
	vdevs[$i]=${dirs[$i]}/$(tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 200)
	log_must truncate -s $MINVDEVSIZE ${vdevs[$i]}
done

log_must zpool create -f -o ashift=12 $TESTPOOL ${vdevs[1]} ${vdevs[2]} \
    ${vdevs[3]} ${vdevs[4]} ${vdevs[5]} ${vdevs[6]} ${vdevs[7]} ${vdevs[8]}
log_must zfs create -o recordsize=128k -o compression=off $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

set_tunable64 METASLAB_FORCE_GANGING 4097
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
log_must dd if=/dev/urandom of=$mountpoint/file bs=128k count=8
log_must zpool sync $TESTPOOL
[[ "$(get_pool_prop feature@dynamic_gang_header $TESTPOOL)" == "active" ]] || \
	log_fail "dynamic_gang_header is not active"

# Rewrite the config with the feature active.
log_must zpool add -f $TESTPOOL ${vdevs[9]}
log_must zpool sync $TESTPOOL

log_must zpool export $TESTPOOL
typeset -a dargs
for i in $(seq 1 9); do
	dargs[$i]="-d ${dirs[$i]}"
done
log_must zpool import ${dargs[*]} $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "A pool with large gang headers in its MOS can be imported"
