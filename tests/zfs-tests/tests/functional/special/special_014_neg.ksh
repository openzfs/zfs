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
# Copyright (c) 2013 by Delphix. All rights reserved.
# Copyright (c) 2016, Intel Corporation.
#

. $STF_SUITE/tests/functional/special/special.kshlib

#
# DESCRIPTION:
#	Ensure you can't create a pool with segregated and dedicated allocation classes.
#

verify_runnable "global"

log_assert "Creating a pool with a dedicated and segrated version of the same class fails."
log_onexit cleanup

for option in "" "-f"
do
	for ac_type in "special" "log"
	do
		log_mustnot zpool create $TESTPOOL $option -o segregate_${ac_type}=on $ZPOOL_DISKS \
		$ac_type mirror $MD_DISKS
	done
done

log_pass "Creating a pool with a dedicated and segregated version of the same class fails."
