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
