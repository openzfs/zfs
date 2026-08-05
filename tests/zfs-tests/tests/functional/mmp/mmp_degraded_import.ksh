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

# DESCRIPTION:
#	Verify the MMP uberblock claim requires a write only to those mirror
#	legs the pool configuration still expects to be present.
#
# STRATEGY:
#	1. Create a mirrored pool with multihost=on and export it without
#	   updating the labels, so it still appears imported and the next
#	   import runs an activity check and an uberblock claim.
#	2. A healthy mirror is claimed and imported.
#	3. A mirror with an offlined leg is claimed and imported degraded.
#	   The offline state is recorded in the configuration, so that leg
#	   is not required and a degraded pool can still fail over.
#	4. A mirror with a leg this host cannot open, and which the
#	   configuration does not mark absent, is refused.  Requiring that
#	   leg is what prevents two hosts which each see one leg from
#	   importing the same pool.
#	5. The same two cases on a three-way mirror, where the number of legs
#	   the configuration expects and the number this host can reach come
#	   apart.  A claim which stopped at two writes would accept the last
#	   case, since two of the three legs are readable.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

#
# A leg moved out of the search directory entirely.  Renaming it in place
# would not do: the devices are matched by the labels they carry rather than by
# name, so a renamed leg is still found and opened.
#
typeset HIDDEN_LEG=$TEST_BASE_DIR/mmp_degraded_hidden_leg

function cleanup
{
	mmp_pool_destroy $MMP_POOL $MMP_DIR
	log_must rm -f $HIDDEN_LEG
	log_must mmp_clear_hostid
}

log_assert "mmp claim requires only the mirror legs the config expects"
log_onexit cleanup

#
# Leave the pool looking imported by exporting it without updating the
# labels, then take on a different identity.  The next import therefore
# runs an activity check and an uberblock claim, as it would after the
# original host failed.
#
function crash_export_as_other_host
{
	log_must zpool export -F $MMP_POOL
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID2
}

# 1. A healthy mirror is claimed and imported
log_note "Verify a healthy mirror is claimed and imported"
mmp_pool_create_simple $MMP_POOL $MMP_DIR
crash_export_as_other_host
log_must zpool import -d $MMP_DIR -f $MMP_POOL
log_must check_state $MMP_POOL "" "ONLINE"
mmp_pool_destroy $MMP_POOL $MMP_DIR

# 2. A mirror with an offlined leg is claimed and imported degraded
log_note "Verify a mirror with an offlined leg is claimed and imported"
mmp_pool_create_simple $MMP_POOL $MMP_DIR
log_must zpool offline $MMP_POOL $MMP_DIR/vdev2
log_must check_state $MMP_POOL $MMP_DIR/vdev2 "OFFLINE"
crash_export_as_other_host
log_must zpool import -d $MMP_DIR -f $MMP_POOL
log_must check_state $MMP_POOL "" "DEGRADED"
log_must check_state $MMP_POOL $MMP_DIR/vdev2 "OFFLINE"
mmp_pool_destroy $MMP_POOL $MMP_DIR

# 3. A mirror with a leg which is unreadable, and not marked absent by
#    the configuration, is refused
log_note "Verify a mirror with an unreachable leg is refused"
mmp_pool_create_simple $MMP_POOL $MMP_DIR
crash_export_as_other_host
log_must mv $MMP_DIR/vdev2 $HIDDEN_LEG

typeset out
if out=$(zpool import -d $MMP_DIR -f $MMP_POOL 2>&1); then
	log_fail "Imported a mirror while a required leg was unreadable"
fi
if ! echo "$out" | grep -q "could not be written to a device"; then
	log_fail "Import refused for an unexpected reason: $out"
fi

#
# The cases above use a two-way mirror, where "every present leg" and "two
# legs" are the same number.  A three-way mirror separates them.
#
function mmp_pool_create_3way
{
	log_must mkdir -p $MMP_DIR
	log_must rm -f $MMP_DIR/*
	log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2 \
	    $MMP_DIR/vdev3
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID1
	log_must zpool create -f -o cachefile=$MMP_CACHE $MMP_POOL \
	    mirror $MMP_DIR/vdev1 $MMP_DIR/vdev2 $MMP_DIR/vdev3
	log_must zpool set multihost=on $MMP_POOL
}

log_must rm -f $HIDDEN_LEG

# 4. A three-way mirror with an offlined leg is claimed and imported degraded
log_note "Verify a three-way mirror with an offlined leg is claimed"
mmp_pool_create_3way
log_must zpool offline $MMP_POOL $MMP_DIR/vdev3
log_must check_state $MMP_POOL $MMP_DIR/vdev3 "OFFLINE"
crash_export_as_other_host
log_must zpool import -d $MMP_DIR -f $MMP_POOL
log_must check_state $MMP_POOL "" "DEGRADED"
log_must check_state $MMP_POOL $MMP_DIR/vdev3 "OFFLINE"
mmp_pool_destroy $MMP_POOL $MMP_DIR

#
# 5. A three-way mirror with one leg this host cannot open, and which the
#    configuration does not mark absent, is refused.  Two of the three legs
#    are readable here, so a claim which required only two writes would let
#    this import while another host holding the third leg could do the same.
#
log_note "Verify a three-way mirror with an unreachable leg is refused"
mmp_pool_create_3way
crash_export_as_other_host
log_must mv $MMP_DIR/vdev3 $HIDDEN_LEG

if out=$(zpool import -d $MMP_DIR -f $MMP_POOL 2>&1); then
	log_fail "Imported a three-way mirror while a required leg was" \
	    "unreadable"
fi
if ! echo "$out" | grep -q "could not be written to a device"; then
	log_fail "Import refused for an unexpected reason: $out"
fi

log_pass "mmp claim requires only the mirror legs the config expects"
