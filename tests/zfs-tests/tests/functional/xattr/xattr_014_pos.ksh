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
# Copyright (c) 2025 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# The default xattr should be shown as 'sa', not 'on', for clarity.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Verify that the xattra is shown as 'sa'.
#	3. Manually set the value to 'dir', 'sa', 'on', and 'off'.
#	4. Verify that it is shown as 'dir', 'sa', 'sa', and 'off.
#

log_assert "The default and specific xattr values are displayed correctly."

set -A args "dir" "sa" "on" "off"
set -A display "dir" "sa" "sa" "off"

log_must eval "[[ 'sa' == '$(zfs get -Hpo value xattr $TESTPOOL)' ]]"

for i in `seq 0 3`; do
	log_must zfs set xattr="${args[$i]}" $TESTPOOL
	log_must eval "[[ '${display[$i]}' == '$(zfs get -Hpo value xattr $TESTPOOL)' ]]"
done
log_pass "The default and specific xattr values are displayed correctly."
