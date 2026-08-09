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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Verify zpool subcmds and system readonly properties can't be delegated.
#
# STRATEGY:
#	1. Loop all the zpool subcmds and readonly properties, except permission
#	   'create' & 'destroy'.
#	2. Verify those subcmd or properties can't be delegated.
#

verify_runnable "both"

log_assert "Verify zpool subcmds and system readonly properties can't be " \
	"delegated."
log_onexit restore_root_datasets

set -A invalid_perms	\
	add		remove		list		iostat		\
	status		offline		online 		clear		\
	attach		detach		replace		scrub		\
	export		import		upgrade				\
	type		creation	used		available	\
	referenced	compressratio	mounted

for dtst in $DATASETS ; do
	typeset -i i=0

	while ((i < ${#invalid_perms[@]})); do
		log_mustnot zfs allow $STAFF1 ${invalid_perms[$i]} $dtst

		((i += 1))
	done
done

log_pass "Verify zpool subcmds and system readonly properties passed."
