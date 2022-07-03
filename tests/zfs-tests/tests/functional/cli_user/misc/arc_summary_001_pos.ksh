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
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

is_freebsd && ! python3 -c 'import sysctl' 2>/dev/null && log_unsupported "python3 sysctl module missing"

log_assert "arc_summary generates output and doesn't return an error code"

# Without this, the below checks aren't going to work the way we hope...
set -o pipefail

for arg in "" "-a" "-d" "-p 1" "-g" "-s arc" "-r"; do
	log_must eval "arc_summary $arg > /dev/null"
done

log_must eval "arc_summary | head > /dev/null"
log_must eval "arc_summary | head -1 > /dev/null"

log_pass "arc_summary generates output and doesn't return an error code"
