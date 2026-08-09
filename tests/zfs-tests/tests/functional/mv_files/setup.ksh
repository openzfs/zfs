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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mv_files/mv_files_common.kshlib

verify_runnable "global"

#
# Determine whether this version of the ksh being
# executed has a bug where the limit of background
# processes of 25.
#
(
	pids=
	for _ in $(seq 50); do
		: &
		pids="$pids $!"
	done

	for pid in $pids; do
		wait $pid || exit
	done
) || {
	log_note "ksh background process limit number is 25"
	GANGPIDS=25
}

init_setup $DISK

log_pass
