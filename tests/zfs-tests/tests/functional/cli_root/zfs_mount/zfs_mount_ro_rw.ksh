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
