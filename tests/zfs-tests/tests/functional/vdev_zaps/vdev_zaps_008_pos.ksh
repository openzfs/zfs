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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that the 'rotational' vdev property is readable on spare and
# L2ARC vdevs, which have no per-vdev ZAP, and that its value persists
# across export/import when the spare device is absent.
#
# STRATEGY:
# 1. Create a pool with a mirror, a spare, and an L2ARC device.
# 2. Verify 'rotational' is readable on leaf, virtual (mirror), spare,
#    and L2ARC vdevs.
# 3. Export the pool, remove the spare file, re-import, and verify that
#    'rotational' still reports the same value for the missing spare,
#    proving the value comes from the persisted config.
#

verify_runnable "global"

SPARE="$TEST_BASE_DIR/vz008-spare"
L2C="$TEST_BASE_DIR/vz008-l2c"
VDEV1="$TEST_BASE_DIR/vz008-vdev1"
VDEV2="$TEST_BASE_DIR/vz008-vdev2"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $VDEV1 $VDEV2 $SPARE $L2C
}

log_assert "'rotational' is readable on ZAP-less vdevs and persists absent"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $SPARE $L2C

log_must zpool create -f $TESTPOOL \
    mirror $VDEV1 $VDEV2 \
    cache $L2C \
    spare $SPARE

# Leaf vdev should report rotational.
NR=$(zpool get -H -o value rotational $TESTPOOL $VDEV1)
[[ "$NR" == "on" || "$NR" == "off" ]] ||
    log_fail "leaf $VDEV1: expected on/off, got '$NR'"

# Virtual (mirror) vdev should report rotational.
MIRROR=$(zpool list -v -H $TESTPOOL | awk '$1 ~ /^mirror/ {print $1; exit}')
NR=$(zpool get -H -o value rotational $TESTPOOL "$MIRROR")
[[ "$NR" == "on" || "$NR" == "off" ]] ||
    log_fail "mirror: expected on/off, got '$NR'"

# Spare vdev should report rotational even though it has no ZAP.
NR=$(zpool get -H -o value rotational $TESTPOOL $SPARE)
[[ "$NR" == "on" || "$NR" == "off" ]] ||
    log_fail "spare $SPARE: expected on/off, got '$NR'"

# L2ARC vdev should report rotational even though it has no ZAP.
NR=$(zpool get -H -o value rotational $TESTPOOL $L2C)
[[ "$NR" == "on" || "$NR" == "off" ]] ||
    log_fail "L2ARC $L2C: expected on/off, got '$NR'"

# The value must persist across export/import when the spare is absent.
# Remove the spare file before re-import so that vdev_open() cannot read
# the hardware value and the only source is the persisted config.
NR_BEFORE=$(zpool get -H -o value rotational $TESTPOOL $SPARE)
log_must zpool export $TESTPOOL
log_must rm -f $SPARE
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
NR_AFTER=$(zpool get -H -o value rotational $TESTPOOL $SPARE)
[[ "$NR_BEFORE" == "$NR_AFTER" ]] ||
    log_fail "spare rotational changed across import: $NR_BEFORE -> $NR_AFTER"

log_pass "'rotational' readable on spare/L2ARC vdevs and persists when absent"
