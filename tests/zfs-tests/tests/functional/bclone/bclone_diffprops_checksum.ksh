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
# Copyright (c) 2023 by Pawel Jakub Dawidek
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_block_cloning
verify_crossfs_block_cloning

log_assert "Verify block cloning across datasets with different checksum properties"

log_must zfs set compress=off $TESTSRCFS
log_must zfs set compress=off $TESTDSTFS

for srcprop in "${checksum_prop_vals[@]}"; do
    for dstprop in "${checksum_prop_vals[@]}"; do
        if [[ $srcprop == $dstprop ]]; then
            continue
        fi
        log_must zfs set checksum=$srcprop $TESTSRCFS
        log_must zfs set checksum=$dstprop $TESTDSTFS
        # 15*8=120, which is greater than 113, so we are sure the data won't
        # be embedded into BP.
        # 32767*8=262136, which is larger than a single default recordsize of
        # 131072.
        FILESIZE=$(random_int_between 15 32767)
        FILESIZE=$((FILESIZE * 8))
        bclone_test random $FILESIZE false $TESTSRCDIR $TESTDSTDIR
    done
done

log_must zfs inherit checksum $TESTSRCFS
log_must zfs inherit checksum $TESTDSTFS

log_pass
