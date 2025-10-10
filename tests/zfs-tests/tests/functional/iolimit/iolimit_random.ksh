#! /bin/ksh -p
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
# Copyright (c) 2024 The FreeBSD Foundation
#
# This software was developed by Pawel Dawidek <pawel@dawidek.net>
# under sponsorship from the FreeBSD Foundation.
#

# All iolimit tests take too much time, so as a part of the common.run
# runfile we will execute one random test.
# All tests can be run from the iolimit.run runfile.

. $STF_SUITE/include/libtest.shlib

IOLIMIT_DIR="${STF_SUITE}/tests/functional/iolimit"

IOLIMIT_TEST=$(random_get $(ls -1 $IOLIMIT_DIR/*.ksh | grep -Ev '/(cleanup|setup)\.ksh$'))

log_note "Random test choosen: ${IOLIMIT_TEST}"

. $IOLIMIT_TEST
