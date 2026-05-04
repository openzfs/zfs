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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	The alloc_bias vdev property is readable and settable on top-level vdevs.
#
# STRATEGY:
#	1. Create a pool with one normal mirror and one special mirror.
#	2. Verify alloc_bias getter returns "none" for normal and "special"
#	   for the special mirror.
#	3. Verify alloc_bias is not reported for leaf (child) vdevs.
#	4. Set alloc_bias=none on the special vdev; verify getter returns "none".
#	5. Export and import the pool; verify no "special" section in status.
#	6. Set alloc_bias=dedup on the same vdev; verify getter returns "dedup".
#	7. Export and import the pool; verify "dedup" section appears in status.
#	8. Set alloc_bias=special; verify getter returns "special".
#	9. Export and import; verify "special" section appears again.
#

verify_runnable "global"

claim="alloc_bias vdev property is readable and settable on top-level vdevs"

log_assert $claim
log_onexit cleanup

log_must disk_setup

# One normal mirror (always stays normal) and one special mirror.
# The normal mirror ensures the pool always has normal-class vdevs
# regardless of what we do to the second mirror.
log_must zpool create $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special mirror $CLASS_DISK0 $CLASS_DISK1

# Find the special vdev name (mirror-N) from zpool status.
TVDEV=$(zpool status $TESTPOOL | \
    awk '/special/{found=1} found && /mirror-/{print $1; exit}')
log_note "Special vdev: $TVDEV"
[[ -n "$TVDEV" ]] || log_fail "Could not determine special vdev name"

# Verify initial alloc_bias values.
BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL mirror-0)
[[ "$BIAS" == "none" ]] || \
    log_fail "Normal mirror alloc_bias: expected none, got $BIAS"

BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $TVDEV)
[[ "$BIAS" == "special" ]] || \
    log_fail "Special mirror alloc_bias: expected special, got $BIAS"

# Verify alloc_bias is not reported for a leaf vdev.
LEAF_OUT=$(zpool get -H -o name,value alloc_bias $TESTPOOL \
    $ZPOOL_DISK0 2>&1)
[[ -z "$LEAF_OUT" ]] || \
    log_fail "alloc_bias reported for leaf vdev, got: $LEAF_OUT"

# --- special -> none, verify after export/import ---
log_must zpool set alloc_bias=none $TESTPOOL $TVDEV
BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $TVDEV)
[[ "$BIAS" == "none" ]] || \
    log_fail "After set none: alloc_bias expected none, got $BIAS"

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR -s $TESTPOOL
zpool status $TESTPOOL | grep -q "special" && \
    log_fail "special still shown after alloc_bias=none + reimport"

# --- none -> dedup, verify after export/import ---
log_must zpool set alloc_bias=dedup $TESTPOOL $TVDEV
BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $TVDEV)
[[ "$BIAS" == "dedup" ]] || \
    log_fail "After set dedup alloc_bias expected dedup, got $BIAS"

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR -s $TESTPOOL
zpool status $TESTPOOL | grep -q "dedup" || \
    log_fail "dedup not shown after alloc_bias=dedup + reimport"

# --- dedup -> special, verify after export/import ---
log_must zpool set alloc_bias=special $TESTPOOL $TVDEV
BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $TVDEV)
[[ "$BIAS" == "special" ]] || \
    log_fail "After set special alloc_bias expected special, got $BIAS"

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR -s $TESTPOOL
zpool status $TESTPOOL | grep -q "special" || \
    log_fail "special not shown after alloc_bias=special + reimport"

log_must zpool destroy -f $TESTPOOL
log_pass $claim
