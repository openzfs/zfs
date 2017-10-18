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
# Copyright (c) 2013 by Delphix. All rights reserved.
# Copyright (c) 2016, Intel Corporation.
#

. $STF_SUITE/tests/functional/special/special.kshlib

#
# DESCRIPTION:
#	Detaching a special device from a 2-way mirror should fail
#	and be disabled.
#

verify_runnable "global"

log_assert "Detaching device from 2-way special mirror fails as expected."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for mdtype in "mirror"
		do
			log_must zpool create $TESTPOOL $option $type $ZPOOL_DISKS \
			    special $mdtype $MD_DISKS
			log_mustnot zpool detach $TESTPOOL $MD_DISK1
			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Detaching device from 2-way special mirror successfully fails."
