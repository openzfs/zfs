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
#	Adding a special device with invalid type to a normal pool fails.
#

verify_runnable "global"

log_assert "Adding a special device to a normal pool fails."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for mdtype in "" "raidz" "raidz2"
		do
			log_must zpool create $TESTPOOL $option $type $ZPOOL_DISKS
			log_mustnot zpool add $TESTPOOL special $mdtype $MD_DISKS
			log_must display_status $TESTPOOL
			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Adding a special device to a normal pool fails."
