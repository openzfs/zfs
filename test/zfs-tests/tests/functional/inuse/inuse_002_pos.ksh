#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/inuse/inuse.cfg

#
# DESCRIPTION:
# ZFS will not interfere with devices that are in use by SVM
#
# STRATEGY:
# 1. Create SVM device d99 with a disk
# 2. Try to create a ZFS pool with same disk
# 3. Try a use the same disk as a spare device
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2

	$METASTAT d99 > /dev/null 2>&1
	(( $? == 0 )) && $METACLEAR -f d99

	typeset metadb=""
	typeset i=""

	metadb=`$METADB | $CUT -f6 | $GREP dev | $UNIQ`
	for i in $metadb; do
		$METADB -fd $i > /dev/null 2>&1
	done

	#
	# Tidy up the disks we used.
	#
	cleanup_devices $vdisks $sdisks
}

log_assert "Ensure ZFS does not interfere with devices in use by SVM"

log_onexit cleanup

for num in 0 1 2; do
	eval typeset slice=\${FS_SIDE$num}
	disk=${slice%s*}
	slice=${slice##*s}
	if [[ $WRAPPER == *"smi"* && \
		$disk == ${saved_disk} ]]; then
		cyl=$(get_endslice $disk ${saved_slice})
		log_must set_partition $slice "$cyl" $FS_SIZE $disk
	else
		log_must set_partition $slice "" $FS_SIZE $disk
	fi
	saved_disk=$disk
	saved_slice=$slice
done

log_note "Configuring metadb with $FS_SIDE1"
log_must $METADB -a -f -c 3 $FS_SIDE1

log_note "Configure d99 with $FS_SIDE0"
log_must $METAINIT d99 1 1 $FS_SIDE0

log_note "Attempt to zpool the device in use by SVM"
log_mustnot $ZPOOL create $TESTPOOL1 $FS_SIDE0
log_mustnot poolexists $TESTPOOL1

log_note "Attempt to take device in use by SVM as spare device"
log_mustnot $ZPOOL create $TESTPOOL1 $FS_SIDE2 spare $FS_SIDE0
log_mustnot poolexists $TESTPOOL1

log_note "Attempt to zpool a metadb device in use by SVM"
log_mustnot $ZPOOL create $TESTPOOL2 $FS_SIDE1
log_mustnot poolexists $TESTPOOL2

log_note "Attempt to take device in use by SVM as spare device"
log_mustnot $ZPOOL create $TESTPOOL2 $FS_SIDE2 spare $FS_SIDE1
log_mustnot poolexists $TESTPOOL2

log_pass "Unable to zpool a device in use by SVM"
