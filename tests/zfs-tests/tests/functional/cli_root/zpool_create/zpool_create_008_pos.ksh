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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' have to use '-f' scenarios
#
# STRATEGY:
# 1. Prepare the scenarios
# 2. Create pool without '-f' and verify it fails
# 3. Create pool with '-f' and verify it succeeds
#

verify_runnable "global"

if is_linux; then
	# Versions of libblkid older than 2.27.0 will not always detect member
	# devices of a pool, therefore skip this test case for old versions.
	currentver="$(blkid -v | tr ',' ' ' | awk '/libblkid/ { print $6 }')"
	requiredver="2.27.0"

	if [ "$(printf "$requiredver\n$currentver" | sort -V | head -n1)" ==  \
	    "$currentver" ] && [ "$currentver" != "$requiredver" ]; then
		log_unsupported "libblkid ($currentver) may not detect pools"
	fi
fi

function cleanup
{
	if [[ $exported_pool == true ]]; then
		if [[ $force_pool == true ]]; then
			log_must zpool create \
				-f $TESTPOOL ${disk}${SLICE_PREFIX}${SLICE0}
		else
			log_must zpool import $TESTPOOL
		fi
	fi

	if poolexists $TESTPOOL ; then
                destroy_pool $TESTPOOL
	fi

	if poolexists $TESTPOOL1 ; then
                destroy_pool $TESTPOOL1
	fi

	#
	# recover it back to EFI label
	#
	create_pool $TESTPOOL $disk
	destroy_pool $TESTPOOL

        partition_disk $SIZE $disk 6
}

#
# create overlap slice 0 and 1 on $disk
#
function create_overlap_slice
{
        typeset format_file=$TEST_BASE_DIR/format_overlap.$$
        typeset disk=$1

        echo "partition" >$format_file
        echo "0" >> $format_file
        echo "" >> $format_file
        echo "" >> $format_file
        echo "0" >> $format_file
        echo "200m" >> $format_file
        echo "1" >> $format_file
        echo "" >> $format_file
        echo "" >> $format_file
        echo "0" >> $format_file
        echo "400m" >> $format_file
        echo "label" >> $format_file
        echo "" >> $format_file
        echo "q" >> $format_file
        echo "q" >> $format_file

        format -e -s -d $disk -f $format_file
	typeset -i ret=$?
        rm -fr $format_file

	if (( ret != 0 )); then
                log_fail "unable to create overlap slice."
        fi

        return 0
}

log_assert "'zpool create' have to use '-f' scenarios"
log_onexit cleanup

typeset exported_pool=false
typeset force_pool=false

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

# overlapped slices as vdev need -f to create pool

# Make the disk is EFI labeled first via pool creation
create_pool $TESTPOOL $disk
destroy_pool $TESTPOOL

if ! is_linux; then
	# Make the disk is VTOC labeled since only VTOC label supports overlap
	log_must labelvtoc $disk
	log_must create_overlap_slice $disk

	unset NOINUSE_CHECK
	log_mustnot zpool create $TESTPOOL ${disk}${SLICE_PREFIX}${SLICE0}
	log_must zpool create -f $TESTPOOL ${disk}${SLICE_PREFIX}${SLICE0}
	destroy_pool $TESTPOOL
fi

# exported device to be as spare vdev need -f to create pool

log_must zpool create -f $TESTPOOL $disk
destroy_pool $TESTPOOL
log_must partition_disk $SIZE $disk 6
create_pool $TESTPOOL ${disk}${SLICE_PREFIX}${SLICE0} \
	${disk}${SLICE_PREFIX}${SLICE1}
log_must zpool export $TESTPOOL
exported_pool=true
log_mustnot zpool create $TESTPOOL1 ${disk}${SLICE_PREFIX}${SLICE3} \
	spare ${disk}${SLICE_PREFIX}${SLICE1}
create_pool $TESTPOOL1 ${disk}${SLICE_PREFIX}${SLICE3} \
	spare ${disk}${SLICE_PREFIX}${SLICE1}
force_pool=true
destroy_pool $TESTPOOL1

log_pass "'zpool create' have to use '-f' scenarios"
