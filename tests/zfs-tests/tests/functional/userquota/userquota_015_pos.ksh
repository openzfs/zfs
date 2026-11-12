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

export LC_ALL="C"
export LANG="C"

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify that Linux quota(1) and setquota(8) query and modify ZFS
#	user, group, and project quotas via quotactl_fd(2).
#
# STRATEGY:
#	1. Query an unconfigured user ID via quota(1) and verify 0 limits/usage.
#	2. Query userquota set by zfs(8) via quota(1).
#	3. Write a file and verify block and inode usage via quota(1).
#	4. Query groupquota set by zfs(8) via quota(1).
#	5. Set user block and inode limits via setquota(8) and verify with
#	   both zfs(8) and quota(1).
#	6. Clear user quota via setquota(8) and verify defaultuserquota
#	   and defaultuserobjquota fallback via quota(1).
#	7. Set, query, and clear group quotas via setquota(8) and quota(1).
#	8. Set, query, and clear project quotas via setquota(8) and quota(1).
#

verify_runnable "both"

function cleanup
{
	zfs set readonly=off $QFS >/dev/null 2>&1
	zfs set defaultuserquota=none $QFS >/dev/null 2>&1
	zfs set defaultuserobjquota=none $QFS >/dev/null 2>&1
	zfs set projectquota@100=none $QFS >/dev/null 2>&1
	zfs set projectobjquota@100=none $QFS >/dev/null 2>&1
	log_must rm -f $QFILE
	cleanup_quota
}

log_onexit cleanup

log_assert "quota(1)/setquota(8) or quotactl_fd query and modify ZFS quotas"

mkmount_writable $QFS
setup_quota_tools

val=$(do_quota "$TESTDIR" usr 999999 bhardlimit)
[[ "$val" -eq 0 ]] || log_fail "Expected bhardlimit 0 for UID 999999, got $val"
val=$(do_quota "$TESTDIR" usr 999999 blocks)
[[ "$val" -eq 0 ]] || log_fail "Expected blocks 0 for UID 999999, got $val"

log_must zfs set userquota@$QUSER1=50M $QFS
sync_pool

val=$(do_quota "$TESTDIR" usr $QUSER1 bhardlimit)
[[ "$val" -eq 51200 ]] || log_fail "Expected bhardlimit 51200, got $val"

log_must user_run $QUSER1 mkfile 10M $QFILE
sync_pool

curblocks=$(do_quota "$TESTDIR" usr $QUSER1 blocks)
[[ "$curblocks" -ge 10240 ]] || \
    log_fail "Expected blocks >= 10240, got $curblocks"

curfiles=$(do_quota "$TESTDIR" usr $QUSER1 files)
[[ "$curfiles" -ge 1 ]] || \
    log_fail "Expected files >= 1, got $curfiles"

log_must zfs set groupquota@$QGROUP=100M $QFS
sync_pool

gval=$(do_quota "$TESTDIR" grp $QGROUP bhardlimit)
[[ "$gval" -eq 102400 ]] || \
    log_fail "Expected group bhardlimit 102400, got $gval"

log_must do_setquota "$TESTDIR" usr $QUSER1 204800 1000
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 209715200 ]] || \
    log_fail "Expected zfs userquota 209715200, got $zfs_val"

zfs_ival=$(zfs get -Hpo value userobjquota@$QUSER1 $QFS)
[[ "$zfs_ival" -eq 1000 ]] || \
    log_fail "Expected zfs userobjquota 1000, got $zfs_ival"

bval=$(do_quota "$TESTDIR" usr $QUSER1 bhardlimit)
[[ "$bval" -eq 204800 ]] || log_fail "Expected bhardlimit 204800, got $bval"

ival=$(do_quota "$TESTDIR" usr $QUSER1 ihardlimit)
[[ "$ival" -eq 1000 ]] || log_fail "Expected ihardlimit 1000, got $ival"

log_must do_setquota "$TESTDIR" usr $QUSER1 0 0
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 0 || "$zfs_val" == "none" ]] || \
    log_fail "Expected cleared userquota, got $zfs_val"

zfs_ival=$(zfs get -Hpo value userobjquota@$QUSER1 $QFS)
[[ "$zfs_ival" -eq 0 || "$zfs_ival" == "none" ]] || \
    log_fail "Expected cleared userobjquota, got $zfs_ival"

log_must zfs set defaultuserquota=25M defaultuserobjquota=500 $QFS
sync_pool

defbval=$(do_quota "$TESTDIR" usr $QUSER1 bhardlimit)
[[ "$defbval" -eq 25600 ]] || \
    log_fail "Expected default bhardlimit 25600, got $defbval"

defival=$(do_quota "$TESTDIR" usr $QUSER1 ihardlimit)
[[ "$defival" -eq 500 ]] || \
    log_fail "Expected default ihardlimit 500, got $defival"

log_must zfs set defaultuserquota=none defaultuserobjquota=none $QFS

log_must do_setquota "$TESTDIR" grp $QGROUP 153600 800
sync_pool

zfs_gval=$(zfs get -Hpo value groupquota@$QGROUP $QFS)
[[ "$zfs_gval" -eq 157286400 ]] || \
    log_fail "Expected zfs groupquota 157286400, got $zfs_gval"

gval=$(do_quota "$TESTDIR" grp $QGROUP bhardlimit)
[[ "$gval" -eq 153600 ]] || \
    log_fail "Expected group bhardlimit 153600, got $gval"

gival=$(do_quota "$TESTDIR" grp $QGROUP ihardlimit)
[[ "$gival" -eq 800 ]] || \
    log_fail "Expected group ihardlimit 800, got $gival"

log_must do_setquota "$TESTDIR" grp $QGROUP 0 0

log_must do_setquota "$TESTDIR" prj 100 102400 300
sync_pool

zfs_pval=$(zfs get -Hpo value projectquota@100 $QFS)
[[ "$zfs_pval" -eq 104857600 ]] || \
    log_fail "Expected zfs projectquota 104857600, got $zfs_pval"

pval=$(do_quota "$TESTDIR" prj 100 bhardlimit)
[[ "$pval" -eq 102400 ]] || \
    log_fail "Expected project bhardlimit 102400, got $pval"

pival=$(do_quota "$TESTDIR" prj 100 ihardlimit)
[[ "$pival" -eq 300 ]] || \
    log_fail "Expected project ihardlimit 300, got $pival"

log_must do_setquota "$TESTDIR" prj 100 0 0

log_pass "ZFS quotas correctly queried and modified via quotactl_fd(2)"
