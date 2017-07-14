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

verify_runnable "global"

if ! $(is_physical_device $DISKS) ; then
	log_unsupported "This directory cannot be run on raw files."
fi

if [[ -n $DISK ]]; then
	#
        # Use 'zpool create' to clean up the information in
        # in the given disk to avoid slice overlapping.
        #
	cleanup_devices $DISK

        partition_disk $((($MINVDEVSIZE / (1024 * 1024)) * 2))m $DISK 7
else
	for disk in `echo $DISKSARRAY`; do
		cleanup_devices $disk

		partition_disk $((($MINVDEVSIZE / (1024 * 1024)) * 2))m $disk 7
	done
fi

log_pass
