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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/slog/slog.cfg

verify_runnable "global"

if ! verify_slog_support ; then
	log_unsupported "This system doesn't support separate intent logs"
fi

if datasetexists $TESTPOOL ; then
	log_must $ZPOOL destroy -f $TESTPOOL
fi
if datasetexists $TESTPOOL2 ; then
	log_must $ZPOOL destroy -f $TESTPOOL2
fi
if [[ -d $VDIR ]]; then
	log_must $RM -rf $VDIR
fi
if [[ -d $VDIR2 ]]; then
	log_must $RM -rf $VDIR2
fi

log_pass
