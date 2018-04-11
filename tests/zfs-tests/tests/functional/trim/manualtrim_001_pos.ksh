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
# Copyright (c) 2013, 2014 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.cfg
. $STF_SUITE/tests/functional/trim/trim.kshlib

set_tunable zfs_trim_min_ext_sz 4096

function getsizemb
{
	typeset rval

	rval=$(du --block-size 1048576 -s "$1" | sed -e 's;[ 	].*;;')
	echo -n "$rval"
}

function checkvdevs
{
	typeset vd sz

	for vd in $VDEVS; do
		sz=$(getsizemb $vd)
		log_note Size of $vd is $sz MB
		log_must test $sz -le $SHRUNK_SIZE_MB
	done
}

function dotrim
{
	log_must rm "/$TRIMPOOL/$TESTFILE"
	log_must zpool export $TRIMPOOL
	log_must zpool import -d $VDEVDIR $TRIMPOOL
	log_must zpool trim $TRIMPOOL
	sleep 5
	log_must zpool export $TRIMPOOL
}

#
# Check various pool geometries:  Create the pool, fill it, remove the test file,
# perform a manual trim, export the pool and verify that the vdevs shrunk.
#

#
# raidz
#
for z in 1 2 3; do
	setupvdevs
	log_must zpool create -f $TRIMPOOL raidz$z $VDEVS
	log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c $NUM_WRITES -d R -w
	dotrim
	checkvdevs
done

#
# mirror
#
setupvdevs
log_must zpool create -f $TRIMPOOL mirror $MIRROR_VDEVS_1 mirror $MIRROR_VDEVS_2
log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c $NUM_WRITES -d R -w
dotrim
checkvdevs

#
# stripe
#
setupvdevs
log_must zpool create -f $TRIMPOOL $STRIPE_VDEVS
log_must file_write -o create -f "/$TRIMPOOL/$TESTFILE" -b $BLOCKSIZE -c $NUM_WRITES -d R -w
dotrim
checkvdevs

log_pass Manual TRIM successfully shrunk vdevs
