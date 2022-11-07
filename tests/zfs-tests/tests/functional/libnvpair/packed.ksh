#! /bin/ksh -p
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
# Copyright 2022 SRI International
#
# This software was developed by SRI International, the University of
# Cambridge Computer Laboratory (Department of Computer Science and
# Technology), and Capabilities Limited under Defense Advanced Research
# Projects Agency (DARPA) Contract No. HR001122C0110 ("ETC").
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#       Ensure that nvlists can be packed and unpacked correctly.
#
# STRATEGY:
#       Run all test cases and check against reference outputs
#

verify_runnable "both"

log_assert "Ensure nvlist pack/unpack work"

log_must nvlist_packed -a -r ${STF_SUITE}/tests/functional/libnvpair/refs

log_pass "Packed nvlists round trip and compare to references"
