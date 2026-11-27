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
#	A pool whose compatibility set does not include raidz_expansion_multi
#	refuses several disks at once, and the feature can only be enabled
#	once the compatibility setting allows it.  While a several-disk
#	expansion runs, the feature is listed in the features_for_read of
#	every label, so software that does not know it refuses the pool.
#	Once "zpool attach -w" returns, or once the expansion has completed
#	and the pool is exported, it is no longer listed.
#
#	No older OpenZFS binary is available here, so what older software
#	does with the pool is checked only through the labels.
#
# STRATEGY:
#	1. Create a raidz1 pool with compatibility=openzfs-2.4, which lacks
#	   raidz_expansion_multi.  Two disks at once are refused, and so is
#	   enabling the feature.  With compatibility=off the feature can be
#	   enabled, and two disks at once work.
#	2. Attach two disks with the copy paused and export: the labels list
#	   raidz_expansion_multi in features_for_read.  Import, finish.
#	3. "zpool attach -w" two disks, then export at once: the labels do
#	   not list raidz_expansion_multi, and after import the feature is
#	   "enabled".
#	4. Attach two disks without -w, export as soon as the expansion has
#	   completed: the labels do not list raidz_expansion_multi.
#

verify_runnable "global"

typeset -r dev_size_mb=128
typeset -r multi_guid="org.openzfs:raidz_expansion_multi"

function cleanup
{
	poolexists "$TESTPOOL" && zpool status "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -f $TEST_BASE_DIR/dev-*

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function check_width # <width>
{
	typeset -i found=$(zpool status $TESTPOOL | \
	    grep -c "$TEST_BASE_DIR/dev-")
	if [[ $found -ne $1 ]]; then
		log_fail "expected $1 disks in $TESTPOOL, found $found"
	fi
}

function check_feature # <feature> <state>
{
	typeset state=$(zpool get -H -o value feature@$1 $TESTPOOL)
	if [[ "$state" != "$2" ]]; then
		log_fail "$1 is $state, expected $2"
	fi
}

#
# Print the features that the label of a device requires for reading.
# zdb -l prints the label config indented by 4, and the members of the
# features_for_read nvlist indented by 4 more, one name per line.
#
function label_features_for_read # <device>
{
	zdb -l $1 | awk '
	    /^ *features_for_read:$/ { n = index($0, "f"); next }
	    n && match($0, /^ */) && RLENGTH >= n { print $1; next }
	    { n = 0 }'
}

#
# Check that the label of every device lists (or does not list) the
# raidz_expansion_multi feature.  A label of an expanded raidz always lists
# raidz_expansion, which shows that the list was read at all.
#
function check_label_multi # <yes|no> <device>...
{
	typeset want=$1
	shift

	for dev in "$@"; do
		typeset fr=$(label_features_for_read $dev)
		if ! echo "$fr" | grep -qx "org.openzfs:raidz_expansion"; then
			log_fail "no raidz_expansion in the label of $dev: $fr"
		fi
		if echo "$fr" | grep -qx "$multi_guid"; then
			[[ $want == "yes" ]] || log_fail \
			    "$multi_guid in the label of $dev: $fr"
		else
			[[ $want == "no" ]] || log_fail \
			    "no $multi_guid in the label of $dev: $fr"
		fi
	done
}

log_onexit cleanup

log_assert "raidz_expansion_multi follows the compatibility property and" \
    "is only in the labels while it is needed"

for i in {1..11}; do
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-$i
done
d=$TEST_BASE_DIR/dev-

# 1. A compatibility set without the feature
log_must zpool create -f -o cachefile=none -o compatibility=openzfs-2.4 \
    $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
check_feature raidz_expansion enabled
check_feature raidz_expansion_multi disabled
log_mustnot_expect "raidz_expansion_multi" \
    zpool attach $TESTPOOL raidz1-0 ${d}4 ${d}5
check_width 3
log_mustnot_expect "compatibility" \
    zpool set feature@raidz_expansion_multi=enabled $TESTPOOL
check_feature raidz_expansion_multi disabled
log_must zpool set compatibility=off $TESTPOOL
log_must zpool set feature@raidz_expansion_multi=enabled $TESTPOOL
check_feature raidz_expansion_multi enabled
log_must zpool attach -w $TESTPOOL raidz1-0 ${d}4 ${d}5
check_width 5
check_feature raidz_expansion_multi enabled
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must zpool destroy $TESTPOOL

# 2. While the copy runs, the labels require the feature for reading
log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 ${d}1 ${d}2 ${d}3
log_must zfs create $TESTPOOL/fs
log_must fill_fs /$TESTPOOL/fs 1 32 1048576 1 R
reflow_size=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((reflow_size / 2))
log_must zpool attach $TESTPOOL raidz1-0 ${d}4 ${d}5
wait_raidz_expand_paused $TESTPOOL
check_feature raidz_expansion_multi active
log_must_busy zpool export $TESTPOOL
check_label_multi yes ${d}1 ${d}2 ${d}3 ${d}4 ${d}5
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
check_width 5
check_feature raidz_expansion_multi enabled

# 3. attach -w, then export at once
log_must zpool attach -w $TESTPOOL raidz1-0 ${d}6 ${d}7
log_must_busy zpool export $TESTPOOL
check_label_multi no ${d}1 ${d}2 ${d}3 ${d}4 ${d}5 ${d}6 ${d}7
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
check_width 7
check_feature raidz_expansion_multi enabled

# 4. Export as soon as the expansion has completed
log_must zpool attach $TESTPOOL raidz1-0 ${d}8 ${d}9
typeset -i t0=$SECONDS
while ! check_pool_status $TESTPOOL "expand" "expanded"; do
	if (( SECONDS - t0 > 300 )); then
		log_fail "the expansion did not complete in 300 seconds"
	fi
	sleep 1
done
log_note "raidz_expansion_multi at export:" \
    "$(zpool get -H -o value feature@raidz_expansion_multi $TESTPOOL)"
log_must_busy zpool export $TESTPOOL
check_label_multi no ${d}1 ${d}2 ${d}3 ${d}4 ${d}5 ${d}6 ${d}7 ${d}8 ${d}9
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
check_width 9
check_feature raidz_expansion_multi enabled
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "raidz_expansion_multi follows the compatibility property and" \
    "is only in the labels while it is needed"
