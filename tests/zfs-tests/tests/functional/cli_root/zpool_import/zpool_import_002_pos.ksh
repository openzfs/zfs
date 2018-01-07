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
# Verify that an exported pool cannot be imported
# more than once.
#
# STRATEGY:
#	1. Populate the default test directory and unmount it.
#	2. Export the default test pool.
#	3. Import it using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	4. Verify it shows up under 'zpool list'.
#	5. Verify it contains a file.
#	6. Attempt to import it for a second time. Verify this fails.
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
		poolexists ${pools[i]} && \
			log_must zpool export ${pools[i]}

		datasetexists "${pools[i]}/$TESTFS" || \
			log_must zpool import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must zfs mount ${pools[i]}/$TESTFS

		[[ -e ${mtpts[i]}/$TESTFILE0 ]] && \
			log_must rm -rf ${mtpts[i]}/$TESTFILE0

		((i = i + 1))
	done

	cleanup_filesystem $TESTPOOL1 $TESTFS

	destroy_pool $TESTPOOL1

	[[ -d $ALTER_ROOT ]] && \
		log_must rm -rf $ALTER_ROOT
}

log_onexit cleanup

log_assert "Verify that an exported pool cannot be imported more than once."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1

checksum1=$(sum $MYTESTFILE | awk '{print $1}')

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

		typeset target=${pools[i]}
		if (( RANDOM % 2 == 0 )) ; then
			target=$guid
			log_note "Import by guid."
		fi

		log_must zpool import ${devs[i]} ${options[j]} $target

		log_must poolexists ${pools[i]}

		log_must ismounted ${pools[i]}/$TESTFS

		basedir=${mtpts[i]}
		[[ -n ${options[j]} ]] && \
			basedir=$ALTER_ROOT/${mtpts[i]}

		[[ ! -e $basedir/$TESTFILE0 ]] && \
			log_fail "$basedir/$TESTFILE0 missing after import."

		checksum2=$(sum $basedir/$TESTFILE0 | awk '{print $1}')
		[[ "$checksum1" != "$checksum2" ]] && \
			log_fail "Checksums differ ($checksum1 != $checksum2)"

		log_mustnot zpool import ${devs[i]} $target

		((j = j + 1))
	done

	((i = i + 1))

done

log_pass "Unable to import the same pool twice as expected."
