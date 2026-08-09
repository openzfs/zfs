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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2014 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_get/zpool_get_parsable.cfg

#
# DESCRIPTION:
#
# Zpool get returns parsable values for all known parsable properties
#
# STRATEGY:
# 1. For all parsable properties, verify zpool get -p returns a parsable value
#

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

typeset -i i=0
typeset class="@(normal|special|dedup|log|elog|special_elog)"
typeset optclass="@(special|dedup|log|elog|special_elog)"

while [[ $i -lt "${#properties[@]}" ]]; do
	log_note "Checking for parsable ${properties[$i]} property"
	log_must eval "zpool get -p ${properties[$i]} $TESTPOOL >/tmp/value.$$"
	log_must grep -q "${properties[$i]}" /tmp/value.$$

	typeset v=$(awk -v p="${properties[$i]}" '$0 ~ p {print $3}' /tmp/value.$$)

	# Determine if this value is a valid number, result in return code
	log_must test -n "$v"
	expr $v + 0 >/dev/null 2>&1

	# All properties must be positive integers in order to be
	# parsable (i.e. a return code of 0 or 1 from expr above).
	# The only exceptions are "expandsize", "class_<class>_expandsize",
	# and "class_<optclass>_fragmentation", which may be "-".
	if [[ ! ($? -eq 0 || $? -eq 1) ]]; then
		case "${properties[$i]}" in
		?(class_${class}_)expandsize) ;&
		class_${optclass}_fragmentation) ;;
		*) log_fail "${properties[$i]} is not parsable"
		esac
	fi

	i=$(( $i + 1 ))
done

rm /tmp/value.$$
log_pass "Zpool get returns parsable values for all known parsable properties"
