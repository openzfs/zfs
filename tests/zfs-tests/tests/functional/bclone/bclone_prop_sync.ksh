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
. $STF_SUITE/include/math.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_block_cloning
verify_crossfs_block_cloning

log_assert "Verify block cloning with all sync property settings"

log_must zfs set compress=zle $TESTSRCFS
log_must zfs set compress=zle $TESTDSTFS

for prop in "${sync_prop_vals[@]}"; do
    log_must zfs set sync=$prop $TESTSRCFS
    # 32767*8=262136, which is larger than a single default recordsize of
    # 131072.
    FILESIZE=$(random_int_between 1 32767)
    FILESIZE=$((FILESIZE * 8))
    bclone_test random $FILESIZE false $TESTSRCDIR $TESTSRCDIR
done

for srcprop in "${sync_prop_vals[@]}"; do
    log_must zfs set sync=$srcprop $TESTSRCFS
    for dstprop in "${sync_prop_vals[@]}"; do
        log_must zfs set sync=$dstprop $TESTDSTFS
        # 32767*8=262136, which is larger than a single default recordsize of
        # 131072.
        FILESIZE=$(random_int_between 1 32767)
        FILESIZE=$((FILESIZE * 8))
        bclone_test random $FILESIZE false $TESTSRCDIR $TESTDSTDIR
    done
done

log_must zfs inherit sync $TESTSRCFS
log_must zfs inherit sync $TESTDSTFS

log_pass
