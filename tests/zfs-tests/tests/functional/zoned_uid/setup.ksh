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
# Copyright 2026 Colin. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

# Only run on Linux - zoned_uid is Linux-specific
if ! is_linux; then
	log_unsupported "zoned_uid is only supported on Linux"
fi

# Check kernel supports user namespaces
if ! [ -f /proc/self/uid_map ]; then
	log_unsupported "The kernel doesn't support user namespaces."
fi

verify_runnable "global"

# Check if zoned_uid property is supported
if ! zoned_uid_supported; then
	log_unsupported "zoned_uid property not supported by this kernel"
fi

DISK=${DISKS%% *}
default_setup $DISK
