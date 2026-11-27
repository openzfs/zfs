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
#	Attaching several disks at once is refused cleanly when it is not
#	allowed, and the raidz width does not change.
#
# STRATEGY:
#	1. raidz_expansion_multi disabled: two disks are refused, one disk
#	   works, and two disks work once the feature is enabled.
#	2. The same disk given twice is refused.
#	3. Two new disks for a mirror, a dRAID vdev or a replace are
#	   refused.  So are vdev type keywords such as "log" among the new
#	   disks.
#	4. Two disks while another expansion is paused are refused.
#	5. raidz1 of 3 plus 11 disks is refused as too wide for one step
#	   (the scratch copy would need more than 16 MiB per old disk);
#	   plus 10 works.
#	6. With ashift=16 the boot area holds 56 sectors: raidz1 of 54
#	   plus 3 disks is refused, saying at most 2 more fit; plus 2 works.
#

verify_runnable "global"

typeset -r dev_size_mb=128

function cleanup
{
	poolexists "$TESTPOOL" && zpool status "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"
	poolexists "$TESTPOOL1" && log_must_busy zpool destroy "$TESTPOOL1"

	log_must rm -f $TEST_BASE_DIR/dev-*

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function check_width # <pool> <width>
{
	typeset -i found=$(zpool status $1 | grep -c "$TEST_BASE_DIR/dev-")
	if [[ $found -ne $2 ]]; then
		log_fail "expected $2 disks in $1, found $found"
	fi
}

log_onexit cleanup

for i in {1..14}; do
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-$i
done
d=$TEST_BASE_DIR/dev-

# 1. Feature disabled
log_must zpool create -f -o cachefile=none \
    -o feature@raidz_expansion_multi=disabled $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
log_mustnot_expect "raidz_expansion_multi" \
    zpool attach $TESTPOOL raidz1-0 ${d}4 ${d}5
check_width $TESTPOOL 3
log_must zpool attach -w $TESTPOOL raidz1-0 ${d}4
check_width $TESTPOOL 4
log_must zpool set feature@raidz_expansion_multi=enabled $TESTPOOL
log_must zpool attach -w $TESTPOOL raidz1-0 ${d}5 ${d}6
check_width $TESTPOOL 6

# 2. The same disk twice
log_mustnot zpool attach $TESTPOOL raidz1-0 ${d}7 ${d}7
check_width $TESTPOOL 6

# 3. Several disks for a mirror, a dRAID vdev or a replace
log_must zpool create -f -o cachefile=none $TESTPOOL1 mirror ${d}8 ${d}9
log_mustnot_expect "raidz" zpool attach $TESTPOOL1 ${d}8 ${d}10 ${d}11
check_width $TESTPOOL1 2
log_must zpool destroy $TESTPOOL1
log_must zpool create -f -o cachefile=none $TESTPOOL1 \
    draid1 ${d}8 ${d}9 ${d}10
draid_vdev=$(zpool status $TESTPOOL1 | awk '$1 ~ /^draid1/ { print $1 }')
log_mustnot_expect "raidz" \
    zpool attach $TESTPOOL1 $draid_vdev ${d}11 ${d}12
check_width $TESTPOOL1 3
log_mustnot_expect "too many arguments" \
    zpool replace $TESTPOOL ${d}1 ${d}10 ${d}11
check_width $TESTPOOL 6
log_must zpool destroy $TESTPOOL1
for kw in log special dedup cache spare mirror; do
	log_mustnot_expect "only devices" \
	    zpool attach $TESTPOOL raidz1-0 ${d}10 $kw ${d}11
	check_width $TESTPOOL 6
done
log_mustnot_expect "only devices" zpool attach $TESTPOOL raidz1-0 log ${d}10
check_width $TESTPOOL 6

# 4. Another expansion is paused
log_must zfs create $TESTPOOL/fs
log_must fill_fs /$TESTPOOL/fs 1 32 1048576 1 R
reflow_size=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((reflow_size / 2))
log_must zpool attach $TESTPOOL raidz1-0 ${d}10 ${d}11
wait_raidz_expand_paused $TESTPOOL
log_mustnot_expect "in progress" zpool attach $TESTPOOL raidz1-0 ${d}12 ${d}13
check_width $TESTPOOL 8
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must zpool destroy $TESTPOOL
log_must rm -f $TEST_BASE_DIR/dev-*

# 5. Too wide for the scratch copy
for i in {1..14}; do
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-$i
done
log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=16
log_mustnot_expect "too wide to add this many devices in one step" \
    zpool attach $TESTPOOL raidz1-0 \
    ${d}4 ${d}5 ${d}6 ${d}7 ${d}8 ${d}9 ${d}10 ${d}11 ${d}12 ${d}13 ${d}14
check_width $TESTPOOL 3
log_must zpool attach -w $TESTPOOL raidz1-0 \
    ${d}4 ${d}5 ${d}6 ${d}7 ${d}8 ${d}9 ${d}10 ${d}11 ${d}12 ${d}13
check_width $TESTPOOL 13
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must zpool destroy $TESTPOOL
log_must rm -f $TEST_BASE_DIR/dev-*

# 6. Too wide for the boot area
typeset -r ashift=16
# VDEV_BOOT_SIZE (3.5M) divided by the sector size
typeset -r maxdevs=$(( (7 << 19) >> ashift ))
typeset -a wide
for i in {1..$((maxdevs + 1))}; do
	wide[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${wide[$i]}
done
log_must zpool create -f -o cachefile=none -o ashift=$ashift $TESTPOOL \
    raidz1 ${wide[1..$((maxdevs - 2))]}
log_mustnot_expect "at most 2 more devices" zpool attach $TESTPOOL raidz1-0 \
    ${wide[$((maxdevs - 1))]} ${wide[$maxdevs]} ${wide[$((maxdevs + 1))]}
check_width $TESTPOOL $((maxdevs - 2))
log_must zpool attach -w $TESTPOOL raidz1-0 \
    ${wide[$((maxdevs - 1))]} ${wide[$maxdevs]}
check_width $TESTPOOL $maxdevs
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL

log_pass "several-disk raidz attach refused cleanly when not allowed"
