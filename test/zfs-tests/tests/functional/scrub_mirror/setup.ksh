#! /usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/scrub_mirror/default.cfg

verify_runnable "global"

if ! $(is_physical_device $DISKS) ; then
	log_unsupported "This directory cannot be run on raw files."
fi

if [[ -n $SINGLE_DISK ]]; then
	log_note "Partitioning a single disk ($SINGLE_DISK)"
else
	log_note "Partitioning disks ($MIRROR_PRIMARY $MIRROR_SECONDARY)"
fi
log_must set_partition ${SIDE_PRIMARY##*s} "" $MIRROR_SIZE $MIRROR_PRIMARY
log_must set_partition ${SIDE_SECONDARY##*s} "" $MIRROR_SIZE $MIRROR_SECONDARY

default_mirror_setup $SIDE_PRIMARY $SIDE_SECONDARY

log_pass
