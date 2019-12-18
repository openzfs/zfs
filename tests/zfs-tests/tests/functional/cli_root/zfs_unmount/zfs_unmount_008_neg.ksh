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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs unmount should fail with bad parameters or scenarios:
#	1. bad option;
#	2. too many arguments;
#	3. null arguments;
#	4. invalid datasets;
#	5. invalid mountpoint;
#	6. already unmounted zfs filesystem;
#	7. legacy mounted zfs filesystem
#
# STRATEGY:
# 1. Make an array of bad parameters
# 2. Use zfs unmount to unmount the filesystem
# 3. Verify that zfs unmount returns error
#

verify_runnable "both"

function cleanup
{
	for ds in $vol $fs1; do
		if datasetexists $ds; then
			log_must zfs destroy -f $ds
		fi
	done

	if snapexists $snap; then
		log_must zfs destroy $snap
	fi

	if [[ -e /tmp/$file ]]; then
		rm -f /tmp/$file
	fi
	if [[ -d /tmp/$dir ]]; then
		rm -rf /tmp/$dir
	fi

}

log_assert "zfs unmount fails with bad parameters or scenarios"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
vol=$TESTPOOL/vol.$$
snap=$TESTPOOL/$TESTFS@snap.$$
set -A badargs "A" "-A" "F" "-F" "-" "-x" "-?"

if ! ismounted $fs; then
	log_must zfs mount $fs
fi

log_must zfs snapshot $snap
if is_global_zone; then
	log_must zfs create -V 10m $vol
else
	vol=""
fi

# Testing bad options
for arg in ${badargs[@]}; do
	log_mustnot eval "zfs unmount $arg $fs >/dev/null 2>&1"
done

# Testing invalid datasets
for ds in $snap $vol "blah"; do
	for opt in "" "-f"; do
		log_mustnot eval "zfs unmount $opt $ds >/dev/null 2>&1"
	done
done

# Testing invalid mountpoint
dir=foodir.$$
file=foo.$$
fs1=$TESTPOOL/fs.$$
mkdir /tmp/$dir
touch /tmp/$file
log_must zfs create -o mountpoint=/tmp/$dir $fs1
curpath=`dirname $0`
cd /tmp
for mpt in "./$dir" "./$file" "/tmp"; do
	for opt in "" "-f"; do
		log_mustnot eval "zfs unmount $opt $mpt >/dev/null 2>&1"
	done
done
cd $curpath

# Testing null argument and too many arguments
for opt in "" "-f"; do
	log_mustnot eval "zfs unmount $opt >/dev/null 2>&1"
	log_mustnot eval "zfs unmount $opt $fs $fs1 >/dev/null 2>&1"
done

# Testing already unmounted filesystem
log_must zfs unmount $fs1
for opt in "" "-f"; do
	log_mustnot eval "zfs unmount $opt $fs1 >/dev/null 2>&1"
	log_mustnot eval "zfs unmount /tmp/$dir >/dev/null 2>&1"
done

# Testing legacy mounted filesystem
log_must zfs set mountpoint=legacy $fs1
if is_linux || is_freebsd; then
	log_must mount -t zfs $fs1 /tmp/$dir
else
	log_must mount -F zfs $fs1 /tmp/$dir
fi
for opt in "" "-f"; do
	log_mustnot eval "zfs unmount $opt $fs1 >/dev/null 2>&1"
done
umount /tmp/$dir

log_pass "zfs unmount fails with bad parameters or scenarios as expected."
