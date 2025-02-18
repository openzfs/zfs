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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
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
# An exported pool can be imported under a different name. Hence
# we test that a previously exported pool can be renamed.
#
# STRATEGY:
#	1. Copy a file into the default test directory.
#	2. Umount the default directory.
#	3. Export the pool.
#	4. Import the pool using the name ${TESTPOOL}-new,
#	   and using the various combinations.
#               - Regular import
#               - Alternate Root Specified
#	5. Verify it exists in the 'zpool list' output.
#	6. Verify the default file system is mounted and that the file
#	   from step (1) is present.
#

verify_runnable "global"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"
set -A options "" "-R $ALTER_ROOT"
set -A mtpts "$TESTDIR" "$TESTDIR1"


function cleanup
{
	typeset -i i=0
	while (( i < ${#pools[*]} )); do
		if poolexists "${pools[i]}-new" ; then
			log_must zpool export "${pools[i]}-new"

			[[ -d /${pools[i]}-new ]] && \
				log_must rm -rf /${pools[i]}-new

			log_must zpool import ${devs[i]} \
				"${pools[i]}-new" ${pools[i]}
		fi

		datasetexists "${pools[i]}" || \
			log_must zpool import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must zfs mount ${pools[i]}/$TESTFS

		[[ -e ${mtpts[i]}/$TESTFILE0 ]] && \
			log_must rm -rf ${mtpts[i]}/$TESTFILE0

		((i = i + 1))

	done

	cleanup_filesystem $TESTPOOL1 $TESTFS $TESTDIR1

	destroy_pool $TESTPOOL1

	[[ -d $ALTER_ROOT ]] && \
		log_must rm -rf $ALTER_ROOT
	[[ -e $VDEV_FILE ]] && \
		log_must rm $VDEV_FILE
}

log_onexit cleanup

log_assert "Verify that an imported pool can be renamed."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1
read -r checksum1 _ < <(cksum $MYTESTFILE)

typeset -i i=0
typeset -i j=0
typeset basedir

while (( i < ${#pools[*]} )); do
	guid=$(get_config ${pools[i]} pool_guid)
	log_must cp $MYTESTFILE ${mtpts[i]}/$TESTFILE0

	log_must zfs umount ${mtpts[i]}

	j=0
	while (( j <  ${#options[*]} )); do
		log_must zpool export ${pools[i]}

		[[ -d /${pools[i]} ]] && \
			log_must rm -rf /${pools[i]}

		typeset target=${pools[i]}
		if (( RANDOM % 2 == 0 )) ; then
			target=$guid
			log_note "Import by guid."
		fi

		log_must zpool import ${devs[i]} ${options[j]} \
			$target ${pools[i]}-new

		log_must poolexists "${pools[i]}-new"

		log_must ismounted ${pools[i]}-new/$TESTFS

		basedir=${mtpts[i]}
		[[ -n ${options[j]} ]] && \
			basedir=$ALTER_ROOT/${mtpts[i]}

		[[ ! -e $basedir/$TESTFILE0 ]] && \
			log_fail "$basedir/$TESTFILE0 missing after import."

		read -r checksum2 _ < <(cksum $basedir/$TESTFILE0)
		log_must [ "$checksum1" = "$checksum2" ]

		log_must zpool export "${pools[i]}-new"

		[[ -d /${pools[i]}-new ]] && \
			log_must rm -rf /${pools[i]}-new

		target=${pools[i]}-new
		if (( RANDOM % 2 == 0 )) ; then
			target=$guid
		fi
		log_must zpool import ${devs[i]} $target ${pools[i]}

		((j = j + 1))
	done

	((i = i + 1))
done

VDEV_FILE=$(mktemp $TEST_BASE_DIR/tmp.XXXXXX)

log_must mkfile -n 128M $VDEV_FILE
log_must zpool create overflow $VDEV_FILE
log_must zfs create overflow/testfs
ID=$(zpool get -Ho value guid overflow)
log_must zpool export overflow
log_mustnot zpool import -d $TEST_BASE_DIR $(echo id) \
    $(printf "%*s\n" 250 "" | tr ' ' 'c')

log_pass "Successfully imported and renamed a ZPOOL"
