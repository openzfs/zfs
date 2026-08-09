#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
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
