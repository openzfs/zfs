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
# Once a pool has been exported, it should be recreated after a
# successful import. Verify that is true.
#
# STRATEGY:
#	1. Populate the default test directory and unmount it.
#	2. Export the default test pool.
#	3. Import it using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	   Try to import it by name or guid randomly.
#	4. Verify it shows up under 'zpool list'.
#	5. Verify it can be mounted again and contains a file.
#

verify_runnable "global"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs " -s" "-d $DEVICE_DIR"
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

log_assert "Verify that an exported pool can be imported."

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

		read -r checksum2 _ < <(cksum $basedir/$TESTFILE0)
		log_must [ "$checksum1" = "$checksum2" ]

		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "Successfully imported a ZPOOL"
