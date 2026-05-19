#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# we set up and mount multiple times, with these combinations:
# - readonly property: on, off
# - mount method: mount(8) (mountpoint=legacy), zfs-mount(8) (mountpoint=path)
# - mount option: [none], ro, rw
#
# after each mount, we check whether we ended up mounting read-only or
# read-write, and note the result. once we've done them all, we compare the
# result set to the "correct" set for this platform (by observation). the
# test passes if they match, fail if they don't
#
#        readonly     |         on          |         off         |
#        mount method |  legacy  |   path   |  legacy  |   path   |
#        mount option | -- ro rw | -- ro rw | -- ro rw | -- ro rw |
typeset -a rs_linux=(   rw ro rw   ro ro rw   rw ro rw   rw ro rw )
typeset -a rs_freebsd=( ro ro ro   ro ro rw   rw ro rw   rw ro rw )

if is_linux ; then
    typeset -n rs_wanted=rs_linux
elif is_freebsd ; then
    typeset -n rs_wanted=rs_freebsd
else
    log_unsupported "no result set defined for this platform"
fi

verify_runnable "both"

testfs=$TESTPOOL/$TESTFS
testmnt=$TESTDIR/mountpoint

function cleanup
{
	log_must zfs inherit -S canmount $testfs
	log_must zfs inherit readonly $testfs
	log_must zfs inherit mountpoint $testfs
	log_must rm -rf $testmnt
}

log_assert "Verify combinations of readonly/readwrite produce correct mount."

log_onexit cleanup


# setup
log_must datasetexists $testfs
log_must zfs set canmount=noauto $testfs
umount $testfs


typeset -a rs=()

for readonly in on off ; do
	for method in legacy path ; do
		for option in default ro rw ; do

			log_must zfs set readonly=$readonly $testfs

			if [[ $method == 'legacy' ]] ; then
				log_must zfs set mountpoint=legacy $testfs
			else
				log_must zfs set mountpoint=$testmnt $testfs
			fi

			# recreate the mountpoint. even if it wasn't mounted,
			# changing the mountpoint property can remove it
			log_must mkdir -p $testmnt

			# issue the mount with the wanted method and option
			case $method in
			legacy)
				case $option in
				default) log_must mount_default $testfs $testmnt ;;
				ro)      log_must mount_ro $testfs $testmnt ;;
				rw)      log_must mount_rw $testfs $testmnt ;;
				esac
			;;
			path)
				case $option in
				default)  log_must zfs mount $testfs ;;
				ro)       log_must zfs mount -o ro $testfs ;;
				rw)       log_must zfs mount -o rw $testfs ;;
				esac
			;;
			esac

			result=$(mount_get_ro_rw $testmnt)
			rs+=($result)
			log_note "result: $result"

			log_must umount $testfs
		done
	done
done

log_note "results: ${rs[@]}"
log_note "wanted:  ${rs_wanted[@]}"

log_must test "${rs[*]}" == "${rs_wanted[*]}"

log_pass "All mounts correct for this platform."
