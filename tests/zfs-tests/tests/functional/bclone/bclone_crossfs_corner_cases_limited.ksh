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
# Copyright (c) 2023 by Pawel Jakub Dawidek
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/bclone/bclone_corner_cases.kshlib

verify_runnable "both"

verify_block_cloning
verify_crossfs_block_cloning

log_assert "Verify various corner cases in block cloning across datasets"

# Disable compression to make sure we won't use embedded blocks.
log_must zfs set compress=off $TESTSRCFS
log_must zfs set recordsize=$RECORDSIZE $TESTSRCFS
log_must zfs set compress=off $TESTDSTFS
log_must zfs set recordsize=$RECORDSIZE $TESTDSTFS

bclone_corner_cases_test $TESTSRCDIR $TESTDSTDIR 100

log_pass
