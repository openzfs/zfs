#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify that ZFS volume property "volmode" works as intended.
#
# STRATEGY:
# 1. Verify "volmode" property does not accept invalid values
# 2. Verify "volmode=none" hides ZVOL device nodes
# 3. Verify "volmode=full" exposes a fully functional device
# 4. Verify "volmode=dev" hides partition info on the device
# 5. Verify "volmode=default" behaves accordingly to "volmode" module parameter
# 6. Verify "volmode" property is inherited correctly
# 7. Verify "volmode" behaves correctly at import time
# 8. Verify "volmode" behaves accordingly to zvol_inhibit_dev (Linux only)
#

verify_runnable "global"

function cleanup
{
	datasetexists $VOLFS && destroy_dataset $VOLFS -r
	datasetexists $ZVOL && destroy_dataset $ZVOL -r
	zfs inherit volmode $TESTPOOL
	sysctl_inhibit_dev 0
	sysctl_volmode 1
	is_linux && udev_cleanup
}

#
# Set zvol_inhibit_dev tunable to $value
#
function sysctl_inhibit_dev # value
{
	typeset value="$1"

	if is_linux; then
		log_note "Setting zvol_inhibit_dev tunable to $value"
		log_must set_tunable32 VOL_INHIBIT_DEV $value
	fi
}

#
# Set volmode tunable to $value
#
function sysctl_volmode # value
{
	typeset value="$1"

	log_note "Setting volmode tunable to $value"
	log_must set_tunable32 VOL_MODE $value
}

#
# Exercise open and close, read and write operations
#
function test_io # dev
{
	typeset dev=$1

	log_must dd if=$dev of=/dev/null count=1
	log_must dd if=/dev/zero of=$dev count=1
}

#
# Changing volmode may need to remove minors, which could be open, so call
# udev_wait() before we "zfs set volmode=<value>".  This ensures no udev
# process has the zvol open (i.e. blkid) and the zvol_remove_minor_impl()
# function won't skip removing the in use device.
#
function set_volmode # value ds
{
	typeset value="$1"
	typeset ds="$2"

	is_linux && udev_wait
	log_must zfs set volmode="$value" "$ds"
}

log_assert "Verify that ZFS volume property 'volmode' works as intended"
log_onexit cleanup

VOLFS="$TESTPOOL/volfs"
ZVOL="$TESTPOOL/vol"
ZDEV="$ZVOL_DEVDIR/$ZVOL"
SUBZVOL="$VOLFS/subvol"
SUBZDEV="$ZVOL_DEVDIR/$SUBZVOL"

# 0. Verify basic ZVOL functionality
log_must zfs create -o mountpoint=none $VOLFS
log_must zfs create -V $VOLSIZE -s $SUBZVOL
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_exists $ZDEV
blockdev_exists $SUBZDEV
test_io $ZDEV
test_io $SUBZDEV

# 1. Verify "volmode" property does not accept invalid values
typeset badvals=("off" "on" "1" "nope" "-")
for badval in ${badvals[@]}
do
	log_mustnot zfs set volmode="$badval" $ZVOL
done

# 2. Verify "volmode=none" hides ZVOL device nodes
set_volmode none $ZVOL
blockdev_missing $ZDEV
log_must_busy zfs destroy $ZVOL
blockdev_missing $ZDEV

# 3. Verify "volmode=full" exposes a fully functional device
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_exists $ZDEV
set_volmode full $ZVOL
blockdev_exists $ZDEV
test_io $ZDEV
log_must verify_partition $ZDEV
# 3.1 Verify "volmode=geom" is an alias for "volmode=full"
set_volmode geom $ZVOL
blockdev_exists $ZDEV
if [[ "$(get_prop 'volmode' $ZVOL)" != "full" ]]; then
	log_fail " Volmode value 'geom' is not an alias for 'full'"
fi
log_must_busy zfs destroy $ZVOL
blockdev_missing $ZDEV

# 4. Verify "volmode=dev" hides partition info on the device
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_exists $ZDEV
set_volmode dev $ZVOL
blockdev_exists $ZDEV
test_io $ZDEV
log_mustnot verify_partition $ZDEV
log_must_busy zfs destroy $ZVOL
blockdev_missing $ZDEV

# 5. Verify "volmode=default" behaves accordingly to "volmode" module parameter
# 5.1 Verify sysctl "volmode=full"
sysctl_volmode 1
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_exists $ZDEV
set_volmode default $ZVOL
blockdev_exists $ZDEV
log_must verify_partition $ZDEV
log_must_busy zfs destroy $ZVOL
blockdev_missing $ZDEV
# 5.2 Verify sysctl "volmode=dev"
sysctl_volmode 2
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_exists $ZDEV
set_volmode default $ZVOL
blockdev_exists $ZDEV
log_mustnot verify_partition $ZDEV
log_must_busy zfs destroy $ZVOL
blockdev_missing $ZDEV
# 5.2 Verify sysctl "volmode=none"
sysctl_volmode 3
log_must zfs create -V $VOLSIZE -s $ZVOL
blockdev_missing $ZDEV
set_volmode default $ZVOL
blockdev_missing $ZDEV

# 6. Verify "volmode" property is inherited correctly
log_must zfs inherit volmode $ZVOL
blockdev_missing $ZDEV
# 6.1 Check volmode=full case
set_volmode full $TESTPOOL
verify_inherited 'volmode' 'full' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.2 Check volmode=none case
set_volmode none $TESTPOOL
verify_inherited 'volmode' 'none' $ZVOL $TESTPOOL
blockdev_missing $ZDEV
# 6.3 Check volmode=dev case
set_volmode dev $TESTPOOL
verify_inherited 'volmode' 'dev' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.4 Check volmode=default case
sysctl_volmode 1
set_volmode default $TESTPOOL
verify_inherited 'volmode' 'default' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.5 Check inheritance on multiple levels
log_must zfs inherit volmode $SUBZVOL
set_volmode none $VOLFS
set_volmode full $TESTPOOL
verify_inherited 'volmode' 'none' $SUBZVOL $VOLFS
blockdev_missing $SUBZDEV
blockdev_exists $ZDEV

# 7. Verify "volmode" behaves correctly at import time
log_must zpool export $TESTPOOL
blockdev_missing $ZDEV
blockdev_missing $SUBZDEV
log_must zpool import $TESTPOOL
blockdev_exists $ZDEV
blockdev_missing $SUBZDEV
log_must_busy zfs destroy $ZVOL
log_must_busy zfs destroy $SUBZVOL
blockdev_missing $ZDEV
blockdev_missing $SUBZDEV

# 8. Verify "volmode" behaves accordingly to zvol_inhibit_dev (Linux only)
if is_linux; then
	sysctl_inhibit_dev 1
	# 7.1 Verify device nodes not are not created with "volmode=full"
	sysctl_volmode 1
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	set_volmode full $ZVOL
	blockdev_missing $ZDEV
	log_must_busy zfs destroy $ZVOL
	blockdev_missing $ZDEV
	# 7.1 Verify device nodes not are not created with "volmode=dev"
	sysctl_volmode 2
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	set_volmode dev $ZVOL
	blockdev_missing $ZDEV
	log_must_busy zfs destroy $ZVOL
	blockdev_missing $ZDEV
	# 7.1 Verify device nodes not are not created with "volmode=none"
	sysctl_volmode 3
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	set_volmode none $ZVOL
	blockdev_missing $ZDEV
fi

log_pass "Verify that ZFS volume property 'volmode' works as intended"
