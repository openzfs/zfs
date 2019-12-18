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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Pools of correct vdev types accept boot property
#
# STRATEGY:
# 1. create pools of each vdev type (raid, raidz, raidz2, mirror + hotspares)
# 2. verify we can set bootfs on each pool type according to design
#

verify_runnable "global"


zpool set 2>&1 | grep bootfs > /dev/null
if [ $? -ne 0 ]
then
	log_unsupported "bootfs pool property not supported on this release."
fi

VDEV1=$TESTDIR/bootfs_006_pos_a.$$.dat
VDEV2=$TESTDIR/bootfs_006_pos_b.$$.dat
VDEV3=$TESTDIR/bootfs_006_pos_c.$$.dat
VDEV4=$TESTDIR/bootfs_006_pos_d.$$.dat

function verify_bootfs { # $POOL
	POOL=$1
	log_must zfs create $POOL/$TESTFS

	log_must zpool set bootfs=$POOL/$TESTFS $POOL
	VAL=$(zpool get bootfs $POOL | tail -1 | awk '{print $3}' )
	if [ $VAL != "$POOL/$TESTFS" ]
	then
		log_must zpool status -v $POOL
		log_fail \
		    "set/get failed on $POOL - expected $VAL == $POOL/$TESTFS"
	fi
	log_must zpool destroy $POOL
}

function verify_no_bootfs { # $POOL
	POOL=$1
	log_must zfs create $POOL/$TESTFS
	log_mustnot zpool set bootfs=$POOL/$TESTFS $POOL
	VAL=$(zpool get bootfs $POOL | tail -1 | awk '{print $3}' )
	if [ $VAL == "$POOL/$TESTFS" ]
	then
		log_must zpool status -v $POOL
		log_fail "set/get unexpectedly failed $VAL != $POOL/$TESTFS"
	fi
	log_must zpool destroy $POOL
}

function cleanup {
	if poolexists $TESTPOOL
	then
		log_must zpool destroy $TESTPOOL
	fi
	log_must rm $VDEV1 $VDEV2 $VDEV3 $VDEV4
}

log_assert "Pools of correct vdev types accept boot property"



log_onexit cleanup
log_must mkfile $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3 $VDEV4


## the following configurations are supported bootable pools

# normal
log_must zpool create $TESTPOOL $VDEV1
verify_bootfs $TESTPOOL

# normal + hotspare
log_must zpool create $TESTPOOL $VDEV1 spare $VDEV2
verify_bootfs $TESTPOOL

# mirror
log_must zpool create $TESTPOOL mirror $VDEV1 $VDEV2
verify_bootfs $TESTPOOL

# mirror + hotspare
log_must zpool create $TESTPOOL mirror $VDEV1 $VDEV2 spare $VDEV3
verify_bootfs $TESTPOOL

if is_linux || is_freebsd; then
	# stripe
	log_must zpool create $TESTPOOL $VDEV1 $VDEV2
	verify_bootfs $TESTPOOL

	# stripe + hotspare
	log_must zpool create $TESTPOOL $VDEV1 $VDEV2 spare $VDEV3
	verify_bootfs $TESTPOOL
else
	## the following configurations are not supported as bootable pools

	# stripe
	log_must zpool create $TESTPOOL $VDEV1 $VDEV2
	verify_no_bootfs $TESTPOOL

	# stripe + hotspare
	log_must zpool create $TESTPOOL $VDEV1 $VDEV2 spare $VDEV3
	verify_no_bootfs $TESTPOOL
fi

# raidz
log_must zpool create $TESTPOOL raidz $VDEV1 $VDEV2
verify_bootfs $TESTPOOL

# raidz + hotspare
log_must zpool create $TESTPOOL raidz $VDEV1 $VDEV2 spare $VDEV3
verify_bootfs $TESTPOOL

# raidz2
log_must zpool create $TESTPOOL raidz2 $VDEV1 $VDEV2 $VDEV3
verify_bootfs $TESTPOOL

# raidz2 + hotspare
log_must zpool create $TESTPOOL raidz2 $VDEV1 $VDEV2 $VDEV3 spare $VDEV4
verify_bootfs $TESTPOOL

log_pass "Pools of correct vdev types accept boot property"
