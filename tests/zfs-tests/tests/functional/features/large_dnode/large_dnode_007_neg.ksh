#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that the dnodesize dataset property won't accept a value
# other than "legacy" if the large_dnode feature is not enabled.
#

verify_runnable "both"

function cleanup
{
        if datasetexists $LGCYPOOL ; then
                log_must zpool destroy -f $LGCYPOOL
        fi
}

log_onexit cleanup

log_assert "values other than dnodesize=legacy rejected by legacy pool"

set -A prop_vals "auto" "1k" "2k" "4k" "8k" "16k"

LGCYPOOL=lgcypool
LGCYFS=$LGCYPOOL/legacy
log_must mkfile 64M  $TESTDIR/$LGCYPOOL
log_must zpool create -d $LGCYPOOL $TESTDIR/$LGCYPOOL
log_must zfs create $LGCYFS

for val in ${prop_vals[@]} ; do
	log_mustnot zfs set dnodesize=$val $LGCYFS
done

log_pass
