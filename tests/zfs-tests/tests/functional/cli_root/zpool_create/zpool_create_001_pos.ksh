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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib
. $TMPFILE

#
# DESCRIPTION:
# 'zpool create <pool> <vspec> ...' can successfully create a
# new pool with a name in ZFS namespace.
#
# STRATEGY:
# 1. Create storage pools with a name in ZFS namespace with different
# vdev specs.
# 2. Verify the pool created successfully
#

verify_runnable "global"

function cleanup
{
	destroy_pool -f $TESTPOOL

	clean_blockfile "$TESTDIR0 $TESTDIR1"

	if [[ -n $DISK ]]; then
		partition_disk $SIZE $DISK 7
	else
		if [[ -n "$LINUX" ]]; then
			DISK0=$DISK0_orig
			DISK1=$DISK1_orig
		fi

		typeset disk=""
		for disk in $DISK0 $DISK1; do
			partition_disk $SIZE $disk 7
			[[ -n "$LINUX" ]] && update_lo_mappings $disk
		done
	fi
}

log_assert "'zpool create <pool> <vspec> ...' can successfully create" \
	"a new pool with a name in ZFS namespace."

log_onexit cleanup

set -A keywords "" "mirror" "raidz" "raidz1"

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

case $DISK_ARRAY_NUM in
0|1)
	typeset disk=""
	if (( $DISK_ARRAY_NUM == 0 )); then
		disk=$DISK
	else
		disk=$DISK0
	fi
	create_blockfile $FILESIZE $TESTDIR0/$FILEDISK0 ${disk}${slice_part}${SLICE5}
        create_blockfile $FILESIZE $TESTDIR1/$FILEDISK1 ${disk}${slice_part}${SLICE6}

	pooldevs="${disk}${slice_part}${SLICE0} \
                  $DEV_DSKDIR/${disk}${slice_part}${SLICE0} \
                  \"${disk}${slice_part}${SLICE0} ${disk}${slice_part}${SLICE1}\" \
                  $TESTDIR0/$FILEDISK0"
	raidzdevs="\"$DEV_DSKDIR/${disk}${slice_part}${SLICE0} ${disk}${slice_part}${SLICE1}\" \
                   \"${disk}${slice_part}${SLICE0} ${disk}${slice_part}${SLICE1} ${disk}${slice_part}${SLICE3}\" \
                   \"${disk}${slice_part}${SLICE0} ${disk}${slice_part}${SLICE1} ${disk}${slice_part}${SLICE3} \
                     ${disk}${slice_part}${SLICE4}\"\
                   \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
	mirrordevs=$raidzdevs
	;;
2|*)
	create_blockfile $FILESIZE $TESTDIR0/$FILEDISK0 ${DISK0}${slice_part}${SLICE5}
        create_blockfile $FILESIZE $TESTDIR1/$FILEDISK1 ${DISK1}${slice_part}${SLICE5}

	pooldevs="${DISK0}${slice_part}${SLICE0}\
                 \"$DEV_DSKDIR/${DISK0}${slice_part}${SLICE0} ${DISK1}${slice_part}${SLICE0}\" \
                 \"${DISK0}${slice_part}${SLICE0} ${DISK0}${slice_part}${SLICE1} ${DISK1}${slice_part}${SLICE1}\"\
                 \"${DISK0}${slice_part}${SLICE0} ${DISK1}${slice_part}${SLICE0} ${DISK0}${slice_part}${SLICE1}\
                   ${DISK1}${slice_part}${SLICE1}\" \
                 \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
	raidzdevs="\"$DEV_DSKDIR/${DISK0}${slice_part}${SLICE0} ${DISK1}${slice_part}${SLICE0}\" \
                 \"${DISK0}${slice_part}${SLICE0} ${DISK0}${slice_part}${SLICE1} ${DISK1}${slice_part}${SLICE1}\"\
                 \"${DISK0}${slice_part}${SLICE0} ${DISK1}${slice_part}${SLICE0} ${DISK0}${slice_part}${SLICE1}\
                   ${DISK1}${slice_part}${SLICE1}\" \
                 \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
	mirrordevs=$raidzdevs
	;;
esac

typeset -i i=0
while (( $i < ${#keywords[*]} )); do
	case ${keywords[i]} in
	"")
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$pooldevs";;
	mirror)
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$mirrordevs";;
	raidz|raidz1)
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$raidzdevs" ;;
	esac
	(( i = i+1 ))
done

log_pass "'zpool create <pool> <vspec> ...' success."
