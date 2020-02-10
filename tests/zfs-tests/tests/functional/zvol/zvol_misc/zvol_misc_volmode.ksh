#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# NOTE: changing volmode may need to remove minors, which could be open, so call
#       udev_wait() before we "zfs set volmode=<value>".

verify_runnable "global"

function cleanup
{
	datasetexists $VOLFS && log_must_busy zfs destroy -r $VOLFS
	datasetexists $ZVOL && log_must_busy zfs destroy -r $ZVOL
	log_must zfs inherit volmode $TESTPOOL
	udev_wait
	sysctl_inhibit_dev 0
	sysctl_volmode 1
	udev_cleanup
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

	log_must dd if=/dev/zero of=$dev count=1
	log_must dd if=$dev of=/dev/null count=1
}

log_assert "Verify that ZFS volume property 'volmode' works as intended"
log_onexit cleanup

VOLFS="$TESTPOOL/volfs"
ZVOL="$TESTPOOL/vol"
ZDEV="${ZVOL_DEVDIR}/$ZVOL"
SUBZVOL="$VOLFS/subvol"
SUBZDEV="${ZVOL_DEVDIR}/$SUBZVOL"

log_must zfs create -o mountpoint=none $VOLFS
log_must zfs create -V $VOLSIZE -s $SUBZVOL
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
test_io $ZDEV
test_io $SUBZDEV

# 1. Verify "volmode" property does not accept invalid values
typeset badvals=("off" "on" "1" "nope" "-")
for badval in ${badvals[@]}
do
	log_mustnot zfs set volmode="$badval" $ZVOL
done

# 2. Verify "volmode=none" hides ZVOL device nodes
log_must zfs set volmode=none $ZVOL
blockdev_missing $ZDEV
log_must_busy zfs destroy $ZVOL

# 3. Verify "volmode=full" exposes a fully functional device
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
log_must zfs set volmode=full $ZVOL
blockdev_exists $ZDEV
test_io $ZDEV
log_must verify_partition $ZDEV
udev_wait
# 3.1 Verify "volmode=geom" is an alias for "volmode=full"
log_must zfs set volmode=geom $ZVOL
blockdev_exists $ZDEV
if [[ "$(get_prop 'volmode' $ZVOL)" != "full" ]]; then
	log_fail " Volmode value 'geom' is not an alias for 'full'"
fi
udev_wait
log_must_busy zfs destroy $ZVOL

# 4. Verify "volmode=dev" hides partition info on the device
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
log_must zfs set volmode=dev $ZVOL
blockdev_exists $ZDEV
test_io $ZDEV
log_mustnot verify_partition $ZDEV
udev_wait
log_must_busy zfs destroy $ZVOL

# 5. Verify "volmode=default" behaves accordingly to "volmode" module parameter
# 5.1 Verify sysctl "volmode=full"
sysctl_volmode 1
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
log_must zfs set volmode=default $ZVOL
blockdev_exists $ZDEV
log_must verify_partition $ZDEV
udev_wait
log_must_busy zfs destroy $ZVOL
# 5.2 Verify sysctl "volmode=dev"
sysctl_volmode 2
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
log_must zfs set volmode=default $ZVOL
blockdev_exists $ZDEV
log_mustnot verify_partition $ZDEV
udev_wait
log_must_busy zfs destroy $ZVOL
# 5.2 Verify sysctl "volmode=none"
sysctl_volmode 3
log_must zfs create -V $VOLSIZE -s $ZVOL
udev_wait
log_must zfs set volmode=default $ZVOL
blockdev_missing $ZDEV

# 6. Verify "volmode" property is inherited correctly
log_must zfs inherit volmode $ZVOL
# 6.1 Check volmode=full case
log_must zfs set volmode=full $TESTPOOL
verify_inherited 'volmode' 'full' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.2 Check volmode=none case
log_must zfs set volmode=none $TESTPOOL
verify_inherited 'volmode' 'none' $ZVOL $TESTPOOL
blockdev_missing $ZDEV
# 6.3 Check volmode=dev case
log_must zfs set volmode=dev $TESTPOOL
verify_inherited 'volmode' 'dev' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.4 Check volmode=default case
sysctl_volmode 1
log_must zfs set volmode=default $TESTPOOL
verify_inherited 'volmode' 'default' $ZVOL $TESTPOOL
blockdev_exists $ZDEV
# 6.5 Check inheritance on multiple levels
log_must zfs inherit volmode $SUBZVOL
udev_wait
log_must zfs set volmode=none $VOLFS
udev_wait
log_must zfs set volmode=full $TESTPOOL
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

# 8. Verify "volmode" behaves accordingly to zvol_inhibit_dev (Linux only)
if is_linux; then
	sysctl_inhibit_dev 1
	# 7.1 Verify device nodes not are not created with "volmode=full"
	sysctl_volmode 1
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	log_must zfs set volmode=full $ZVOL
	blockdev_missing $ZDEV
	log_must_busy zfs destroy $ZVOL
	# 7.1 Verify device nodes not are not created with "volmode=dev"
	sysctl_volmode 2
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	log_must zfs set volmode=dev $ZVOL
	blockdev_missing $ZDEV
	log_must_busy zfs destroy $ZVOL
	# 7.1 Verify device nodes not are not created with "volmode=none"
	sysctl_volmode 3
	log_must zfs create -V $VOLSIZE -s $ZVOL
	blockdev_missing $ZDEV
	log_must zfs set volmode=none $ZVOL
	blockdev_missing $ZDEV
fi

log_pass "Verify that ZFS volume property 'volmode' works as intended"
