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
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#	Verify that quotactl_fd(2) queries and sets ZFS user, group, and
#	project quotas.
#
# STRATEGY:
#	1. Query an unconfigured user ID and verify it fails (ENOENT).
#	2. Query userquota set by zfs CLI via quotactl_fd get.
#	3. Verify curspace after writing a file.
#	4. Query groupquota set by zfs CLI via quotactl_fd get.
#	5. Set user quota via quotactl_fd set and verify with zfs get.
#	6. Set space and inode limits via quotactl_fd set and verify.
#	7. Clear quota via quotactl_fd set and verify defaultuserquota fallback.
#	8. Set and query project quota via quotactl_fd set/get.
#	9. Verify setting quota on a read-only mount fails (EROFS).
#	10. Call quotactl_fd sync and verify it succeeds.
#

verify_runnable "both"

if is_freebsd; then
	log_unsupported "quotactl_fd is a Linux-specific interface"
fi

which quotactl_fd >/dev/null 2>&1
if [[ $? -ne 0 ]]; then
	log_unsupported "quotactl_fd test binary not available"
fi

function cleanup
{
	zfs set readonly=off $QFS >/dev/null 2>&1
	zfs set defaultuserquota=none $QFS >/dev/null 2>&1
	zfs set projectquota@100=none $QFS >/dev/null 2>&1
	log_must rm -f $QFILE
	cleanup_quota
}

log_onexit cleanup

log_assert "quotactl_fd(2) correctly queries and modifies quotas"

mkmount_writable $QFS

out=$(quotactl_fd sync $TESTDIR 2>&1)
if echo "$out" | grep -qi "Function not implemented"; then
	log_unsupported "quotactl_fd(2) syscall not supported on this kernel"
fi

UID_QUSER1=$(id -u $QUSER1)
GID_QGROUP=$(getent group $QGROUP | cut -d: -f3)

# 1. Verify querying an unconfigured ID with no quota/usage fails with ENOENT
log_mustnot quotactl_fd get $TESTDIR usr 999999

# 2. Test querying userquota set by zfs CLI
log_must zfs set userquota@$QUSER1=50M $QFS
sync_pool

val=$(quotactl_fd get $TESTDIR usr $UID_QUSER1 | \
    grep bhardlimit | cut -d= -f2)
# 50M in 1024-byte blocks is 51200
[[ "$val" -eq 51200 ]] || log_fail "Expected bhardlimit 51200, got $val"

# 3. Write 10M file and verify curspace via quotactl_fd
log_must user_run $QUSER1 mkfile 10M $QFILE
sync_pool

curspace=$(quotactl_fd get $TESTDIR usr $UID_QUSER1 | \
    grep curspace | cut -d= -f2)
[[ "$curspace" -ge 10485760 ]] || \
    log_fail "Expected curspace >= 10485760, got $curspace"

# 4. Test querying groupquota set by zfs CLI
if [[ -n "$GID_QGROUP" ]]; then
	log_must zfs set groupquota@$QGROUP=100M $QFS
	sync_pool

	gval=$(quotactl_fd get $TESTDIR grp $GID_QGROUP | \
	    grep bhardlimit | cut -d= -f2)
	# 100M in 1024-byte blocks is 102400
	[[ "$gval" -eq 102400 ]] || \
	    log_fail "Expected group bhardlimit 102400, got $gval"
fi

# 5. Set quota directly using quotactl_fd set (200MB = 209715200 bytes)
log_must quotactl_fd set $TESTDIR usr $UID_QUSER1 209715200
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 209715200 ]] || \
    log_fail "Expected zfs userquota 209715200, got $zfs_val"

# 6. Set space and inode limits via quotactl_fd set
log_must quotactl_fd set $TESTDIR usr $UID_QUSER1 209715200 1000
sync_pool

ival=$(quotactl_fd get $TESTDIR usr $UID_QUSER1 | \
    grep ihardlimit | cut -d= -f2)
[[ "$ival" -eq 1000 ]] || log_fail "Expected ihardlimit 1000, got $ival"

icur=$(quotactl_fd get $TESTDIR usr $UID_QUSER1 | \
    grep curinodes | cut -d= -f2)
[[ "$icur" -ge 1 ]] || log_fail "Expected curinodes >= 1, got $icur"

# 7. Clear user quota via quotactl_fd set (0) and verify default quota fallback
log_must quotactl_fd set $TESTDIR usr $UID_QUSER1 0 0
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 0 || "$zfs_val" == "none" ]] || \
    log_fail "Expected cleared userquota, got $zfs_val"

log_must zfs set defaultuserquota=25M $QFS
sync_pool

defval=$(quotactl_fd get $TESTDIR usr $UID_QUSER1 | \
    grep bhardlimit | cut -d= -f2)
# 25M in 1024-byte blocks is 25600
[[ "$defval" -eq 25600 ]] || \
    log_fail "Expected default bhardlimit 25600, got $defval"

log_must zfs set defaultuserquota=none $QFS

# 8. Set and query project quota via quotactl_fd (100MB = 104857600 bytes)
log_must quotactl_fd set $TESTDIR prj 100 104857600
sync_pool

pval=$(quotactl_fd get $TESTDIR prj 100 | grep bhardlimit | cut -d= -f2)
[[ "$pval" -eq 102400 ]] || \
    log_fail "Expected project bhardlimit 102400, got $pval"

# 9. Verify setting quota on a read-only mount fails with EROFS
log_must zfs set readonly=on $QFS
log_mustnot quotactl_fd set $TESTDIR usr $UID_QUSER1 104857600
log_must zfs set readonly=off $QFS

# 10. Call quotactl_fd sync
log_must quotactl_fd sync $TESTDIR

log_pass "quotactl_fd correctly queried and modified quotas"
