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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	A mirror/raidz/raidz2 cache can not be added to existed pool.
#
# STRATEGY:
#	1. Create pool with or without cache.
#	2. Add a mirror/raidz/raidz2 cache to this pool.
#	3. Verify failed to add.
#

verify_runnable "global"
verify_disk_count "$LDEV2"

log_assert "A raidz/raidz2 cache can not be added to existed pool."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	for cachetype in "mirror" "raidz" "raidz1" "raidz2"
	do
		log_must $ZPOOL create $TESTPOOL $type $VDEV \
			cache $LDEV

		log_mustnot $ZPOOL add $TESTPOOL cache $cachetype $LDEV2
		ldev=$(random_get $LDEV2)
		log_mustnot verify_cache_device \
			$TESTPOOL $ldev 'ONLINE' $cachetype

		log_must $ZPOOL destroy $TESTPOOL
	done
done

log_pass "A mirror/raidz/raidz2 cache can not be added to existed pool."
