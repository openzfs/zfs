#!/bin/ksh
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
# DESCRIPTION
# verify 'zfs snapshot -m<minfree> <filesystem>' works correctly
#
# STRATEGY
# 1. Create a datasets
# 2. Set a refquota
# 3. Attempt to create snapshots with minfree < refquota and see it fail
# 4. Attempt to create snapshots with minfree > refquota and see it succeed

. $STF_SUITE/include/libtest.shlib

function cleanup
{
    datasetexists $TFS && log_must zfs destroy -r "$TFS"
}

TFS="$TESTPOOL/zfs-snapshot-minfree"
quotas="2G"
ok_minfrees="0 1 1K 1M 1G"
fail_minfrees="3G 1T"

log_assert "verify zfs snapshot supports minfree option"
log_onexit cleanup
typeset -i i=1

log_note "testing various minfree sizes"
    
log_must zfs create -o refquota=1G "$TFS"

# Verify -n works
for M in $ok_minfrees $fail_minfrees; do
    TSNAP="${TFS}@T-${M}"
    log_must zfs snapshot -n -m"$M" "$TSNAP"
    log_mustnot snapexists "$TSNAP"
done

# Verify -m succeeds if avail < minfree
for M in $ok_minfrees; do
    TSNAP="${TFS}@T-${M}"
    log_must zfs snapshot -m"$M" "$TSNAP"
    log_must snapexists "$TSNAP"
done

# Verify -m fails if avail < minfree
for M in $fail_minfrees; do
    TSNAP="${TFS}@T-${M}"
    log_must zfs snapshot -m"$M" "$TSNAP"
    log_notmust snapexists "$TSNAP"
done

log_must zfs destroy -r "$TFS"

log_pass "zfs snapshot minfree verified correctly"

