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
# Copyright (c) 2012, 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rename/zfs_rename.kshlib

#
# DESCRIPTION:
#       'zfs rename' should fail when the dataset are not within the same pool
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a different pool.
#       3. Verify the operation fails, and only the original name
#	   is displayed by zfs list.
#

verify_runnable "global"

function my_cleanup
{
	poolexists $TESTPOOL1 && \
		destroy_pool $TESTPOOL1
	[[ -e $TESTDIR/$TESTFILE1 ]] && \
		log_must $RM -f $TESTDIR/$TESTFILE1
	cleanup
}

set -A src_dataset \
    "$TESTPOOL/$TESTFS1" "$TESTPOOL/$TESTCTR1" \
    "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTVOL" \
    "$TESTPOOL/$TESTFS@snapshot" "$TESTPOOL/$TESTFS-clone"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit my_cleanup

log_assert "'zfs rename' should fail while datasets are within different pool."

additional_setup

log_must $MKFILE $MINVDEVSIZE $TESTDIR/$TESTFILE1
create_pool $TESTPOOL1 $TESTDIR/$TESTFILE1

for src in ${src_dataset[@]} ; do
	dest=${src#$TESTPOOL/}
	if [[ $dest == *"@"* ]]; then
		dest=${dest#*@}
		dest=${TESTPOOL1}@$dest
	else
		dest=${TESTPOOL1}/$dest
	fi
	log_mustnot $ZFS rename $src $dest
	log_mustnot $ZFS rename -p $src $dest

	#
	# Verify original dataset name still in use
	#
	log_must datasetexists $src
done

log_pass "'zfs rename' fail while datasets are within different pool."
