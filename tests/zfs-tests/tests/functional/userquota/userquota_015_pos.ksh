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

#
# Query a quota field via quota(1) or quotactl_fd depending on QUOTA_MODE.
#   qtype: usr | grp | prj
#   field: blocks | bhardlimit | files | ihardlimit
#
function get_quota_field
{
	typeset qtype=$1
	typeset id=$2
	typeset field=$3
	typeset flag col line val numid key

	if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
		case "$qtype" in
		usr)	flag="-u" ;;
		grp)	flag="-g" ;;
		prj)	flag="-P" ;;
		esac
		case "$field" in
		blocks)		col=2 ;;
		bhardlimit)	col=4 ;;
		files)		col=6 ;;
		ihardlimit)	col=8 ;;
		esac
		line=$("$QUOTA_CMD" -v -w -p --filesystem="$TESTDIR" \
		    "$flag" "$id" | tail -n 1)
		val=$(echo "$line" | awk -v c="$col" '{print $c}')
		echo "${val%\*}"
		return 0
	fi

	numid=$(quota_id_to_num "$qtype" "$id")
	case "$field" in
	blocks)		key="curspace" ;;
	bhardlimit)	key="bhardlimit" ;;
	files)		key="curinodes" ;;
	ihardlimit)	key="ihardlimit" ;;
	esac
	val=$(quotactl_fd get "$TESTDIR" "$qtype" "$numid" | \
	    awk -F= -v k="$key" '$1 == k {print $2}')
	if [[ "$field" == "blocks" ]]; then
		echo $((val / 1024))
	else
		echo "$val"
	fi
}

function set_quota_limits
{
	typeset qtype=$1
	typeset id=$2
	typeset kb=$3
	typeset inodes=$4
	typeset flag numid

	if [[ "$QUOTA_MODE" == "quota_tools" ]]; then
		case "$qtype" in
		usr)	flag="-u" ;;
		grp)	flag="-g" ;;
		prj)	flag="-P" ;;
		esac
		log_must "$SETQUOTA_CMD" "$flag" "$id" "$kb" "$kb" \
		    "$inodes" "$inodes" "$TESTDIR"
	else
		numid=$(quota_id_to_num "$qtype" "$id")
		log_must quotactl_fd set "$TESTDIR" "$qtype" "$numid" \
		    $((kb * 1024)) "$inodes"
	fi
}

log_onexit cleanup

log_assert "quota(1)/setquota(8) or quotactl_fd query and modify ZFS quotas"

mkmount_writable $QFS
setup_quota_tools

# 1. Verify querying an unconfigured ID with no quota/usage returns 0s
val=$(get_quota_field usr 999999 bhardlimit)
[[ "$val" -eq 0 ]] || log_fail "Expected bhardlimit 0 for UID 999999, got $val"
val=$(get_quota_field usr 999999 blocks)
[[ "$val" -eq 0 ]] || log_fail "Expected blocks 0 for UID 999999, got $val"

# 2. Test querying userquota set by zfs CLI
log_must zfs set userquota@$QUSER1=50M $QFS
sync_pool

val=$(get_quota_field usr $QUSER1 bhardlimit)
# 50M in 1024-byte blocks is 51200
[[ "$val" -eq 51200 ]] || log_fail "Expected bhardlimit 51200, got $val"

# 3. Write 10M file and verify blocks and files
log_must user_run $QUSER1 mkfile 10M $QFILE
sync_pool

curblocks=$(get_quota_field usr $QUSER1 blocks)
# 10M in 1024-byte blocks is 10240
[[ "$curblocks" -ge 10240 ]] || \
    log_fail "Expected blocks >= 10240, got $curblocks"

curfiles=$(get_quota_field usr $QUSER1 files)
[[ "$curfiles" -ge 1 ]] || \
    log_fail "Expected files >= 1, got $curfiles"

# 4. Test querying groupquota set by zfs CLI
log_must zfs set groupquota@$QGROUP=100M $QFS
sync_pool

gval=$(get_quota_field grp $QGROUP bhardlimit)
# 100M in 1024-byte blocks is 102400
[[ "$gval" -eq 102400 ]] || \
    log_fail "Expected group bhardlimit 102400, got $gval"

# 5. Set user block and inode limits (200M = 204800 KiB, 1000 inodes)
set_quota_limits usr $QUSER1 204800 1000
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 209715200 ]] || \
    log_fail "Expected zfs userquota 209715200, got $zfs_val"

zfs_ival=$(zfs get -Hpo value userobjquota@$QUSER1 $QFS)
[[ "$zfs_ival" -eq 1000 ]] || \
    log_fail "Expected zfs userobjquota 1000, got $zfs_ival"

bval=$(get_quota_field usr $QUSER1 bhardlimit)
[[ "$bval" -eq 204800 ]] || log_fail "Expected bhardlimit 204800, got $bval"

ival=$(get_quota_field usr $QUSER1 ihardlimit)
[[ "$ival" -eq 1000 ]] || log_fail "Expected ihardlimit 1000, got $ival"

# 6. Clear user quota and verify default quota fallback
set_quota_limits usr $QUSER1 0 0
sync_pool

zfs_val=$(zfs get -Hpo value userquota@$QUSER1 $QFS)
[[ "$zfs_val" -eq 0 || "$zfs_val" == "none" ]] || \
    log_fail "Expected cleared userquota, got $zfs_val"

zfs_ival=$(zfs get -Hpo value userobjquota@$QUSER1 $QFS)
[[ "$zfs_ival" -eq 0 || "$zfs_ival" == "none" ]] || \
    log_fail "Expected cleared userobjquota, got $zfs_ival"

log_must zfs set defaultuserquota=25M defaultuserobjquota=500 $QFS
sync_pool

defbval=$(get_quota_field usr $QUSER1 bhardlimit)
# 25M in 1024-byte blocks is 25600
[[ "$defbval" -eq 25600 ]] || \
    log_fail "Expected default bhardlimit 25600, got $defbval"

defival=$(get_quota_field usr $QUSER1 ihardlimit)
[[ "$defival" -eq 500 ]] || \
    log_fail "Expected default ihardlimit 500, got $defival"

log_must zfs set defaultuserquota=none defaultuserobjquota=none $QFS

# 7. Set, query, and clear group quotas
set_quota_limits grp $QGROUP 153600 800
sync_pool

zfs_gval=$(zfs get -Hpo value groupquota@$QGROUP $QFS)
[[ "$zfs_gval" -eq 157286400 ]] || \
    log_fail "Expected zfs groupquota 157286400, got $zfs_gval"

gval=$(get_quota_field grp $QGROUP bhardlimit)
[[ "$gval" -eq 153600 ]] || \
    log_fail "Expected group bhardlimit 153600, got $gval"

gival=$(get_quota_field grp $QGROUP ihardlimit)
[[ "$gival" -eq 800 ]] || \
    log_fail "Expected group ihardlimit 800, got $gival"

set_quota_limits grp $QGROUP 0 0

# 8. Set, query, and clear project quotas
set_quota_limits prj 100 102400 300
sync_pool

zfs_pval=$(zfs get -Hpo value projectquota@100 $QFS)
[[ "$zfs_pval" -eq 104857600 ]] || \
    log_fail "Expected zfs projectquota 104857600, got $zfs_pval"

pval=$(get_quota_field prj 100 bhardlimit)
[[ "$pval" -eq 102400 ]] || \
    log_fail "Expected project bhardlimit 102400, got $pval"

pival=$(get_quota_field prj 100 ihardlimit)
[[ "$pival" -eq 300 ]] || \
    log_fail "Expected project ihardlimit 300, got $pival"

set_quota_limits prj 100 0 0

log_pass "ZFS quotas correctly queried and modified via quotactl_fd(2)"
