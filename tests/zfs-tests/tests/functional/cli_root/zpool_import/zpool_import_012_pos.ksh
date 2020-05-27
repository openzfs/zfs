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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
# Once a pool has been exported, it should be recreated after a
# successful import, all the sub-filesystems within it should all be restored,
# include mount & share status. Verify that is true.
#
# STRATEGY:
#	1. Create the test pool and hierarchical filesystems.
#	2. Export the test pool, or destroy the test pool,
#		depend on testing import [-Df].
#	3. Import it using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	4. Verify the mount & share status is restored.
#

verify_runnable "global"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"
set -A options "" "-R $ALTER_ROOT"
set -A mtpts "$TESTDIR" "$TESTDIR1"


function cleanup
{
	typeset -i i=0

	while ((i < ${#pools[*]})); do
		if poolexists ${pools[i]}; then
			log_must zpool export ${pools[i]}
			log_note "Try to import ${devs[i]} ${pools[i]}"
			zpool import ${devs[i]} ${pools[i]}
		else
			log_note "Try to import $option ${devs[i]} ${pools[i]}"
			zpool import $option ${devs[i]} ${pools[i]}
		fi

		if poolexists ${pools[i]}; then
			is_shared ${pools[i]} && \
			    log_must zfs set sharenfs=off ${pools[i]}

			ismounted "${pools[i]}/$TESTFS" || \
			    log_must zfs mount ${pools[i]}/$TESTFS
		fi

		((i = i + 1))
	done

	destroy_pool $TESTPOOL1

	if datasetexists $TESTPOOL/$TESTFS; then
		log_must zfs destroy -Rf $TESTPOOL/$TESTFS
	fi
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	[[ -d $ALTER_ROOT ]] && \
		log_must rm -rf $ALTER_ROOT
}

log_onexit cleanup

log_assert "Verify all mount & share status of sub-filesystems within a pool \
	can be restored after import [-Df]."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1
# create a hierarchy of filesystem
for pool in ${pools[@]} ; do
	log_must zfs create $pool/$TESTFS/$TESTCTR
	log_must zfs create $pool/$TESTFS/$TESTCTR/$TESTCTR1
	log_must zfs set canmount=off $pool/$TESTFS/$TESTCTR
	log_must zfs set canmount=off $pool/$TESTFS/$TESTCTR/$TESTCTR1
	log_must zfs create $pool/$TESTFS/$TESTCTR/$TESTFS1
	log_must zfs create $pool/$TESTFS/$TESTCTR/$TESTCTR1/$TESTFS1
	log_must zfs create $pool/$TESTFS/$TESTFS1
	log_must zfs snapshot $pool/$TESTFS/$TESTFS1@snap
	log_must zfs clone $pool/$TESTFS/$TESTFS1@snap $pool/$TESTCLONE1
done

typeset mount_fs="$TESTFS $TESTFS/$TESTFS1 $TESTCLONE1 \
		$TESTFS/$TESTCTR/$TESTFS1 $TESTFS/$TESTCTR/$TESTCTR1/$TESTFS1"
typeset nomount_fs="$TESTFS/$TESTCTR $TESTFS/$TESTCTR/$TESTCTR1"

typeset -i i=0
typeset -i j=0
typeset -i nfs_share_bit=0
typeset -i guid_bit=0
typeset basedir

for option in "" "-Df"; do
	i=0
	while ((i < ${#pools[*]})); do
		pool=${pools[i]}
		guid=$(get_pool_prop guid $pool)
		j=0
		while ((j < ${#options[*]})); do
			# set sharenfs property off/on
			nfs_share_bit=0
			while ((nfs_share_bit <= 1)); do
				typeset f_share=""
				typeset nfs_flag="sharenfs=off"
				if ((nfs_share_bit == 1)); then
					log_note "Set sharenfs=on $pool"
					log_must zfs set sharenfs=on $pool
					! is_freebsd && log_must is_shared $pool
					f_share="true"
					nfs_flag="sharenfs=on"
				fi
				# for every off/on nfs bit import guid/pool_name
				guid_bit=0
				while ((guid_bit <= 1)); do
					typeset guid_flag="pool name"
					if [[ -z $option ]]; then
						log_must_busy zpool export $pool
					else
						log_must_busy zpool destroy $pool
					fi

					typeset target=$pool
					if ((guid_bit == 1)); then
						log_note "Import by guid."
						if [[ -z $guid ]]; then
							log_fail "guid should "\
							    "not be empty!"
						else
							target=$guid
							guid_flag="$guid"
						fi
					fi
					log_note "Import with $nfs_flag and " \
					    "$guid_flag"
					zpool import $option ${devs[i]} \
					    ${options[j]} $target
					#import by GUID if import by pool name fails
					if [[ $? != 0 ]]; then
						log_note "Possible pool name" \
						    "duplicates. Try GUID import"
						target=$guid
						log_must zpool import $option \
						    ${devs[i]} ${options[j]} \
						    $target
					fi
					log_must poolexists $pool

					for fs in $mount_fs; do
						log_must ismounted $pool/$fs
						[[ -n $f_share ]] && \
						    ! is_freebsd && \
						    log_must is_shared $pool/$fs
					done

					for fs in $nomount_fs; do
						log_mustnot ismounted $pool/$fs
						! is_freebsd && \
						    log_mustnot is_shared $pool/$fs
					done
					((guid_bit = guid_bit + 1))
				done
				# reset nfsshare=off
				if [[ -n $f_share ]]; then
					log_must zfs set sharenfs=off $pool
					! is_freebsd && log_mustnot is_shared $pool
				fi
				((nfs_share_bit = nfs_share_bit + 1))
			done

			((j = j + 1))
		done

		((i = i + 1))
	done
done

log_pass "All mount & share status of sub-filesystems within a pool \
	can be restored after import [-Df]."
