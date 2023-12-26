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

log_assert "Verify block cloning across datasets with different recordsize properties"

log_must zfs set compress=off $TESTSRCFS
log_must zfs set compress=off $TESTDSTFS

# recsize_prop_vals[] array contains too many entries and the tests take too
# long. Let's use only a subset of them.
typeset -a bclone_recsize_prop_vals=('512' '4096' '131072' '1048576')

for srcprop in "${bclone_recsize_prop_vals[@]}"; do
    for dstprop in "${bclone_recsize_prop_vals[@]}"; do
        if [[ $srcprop == $dstprop ]]; then
            continue
        fi
        log_must zfs set recordsize=$srcprop $TESTSRCFS
        log_must zfs set recordsize=$dstprop $TESTDSTFS
        # 2*64=128, which is greater than 113, so we are sure the data won't
        # be embedded into BP.
        # 32767*64=2097088, which is larger than the largest recordsize (1MB).
        FILESIZE=$(random_int_between 2 32767)
        FILESIZE=$((FILESIZE * 64))
        bclone_test random $FILESIZE false $TESTSRCDIR $TESTDSTDIR
    done
done

log_must zfs inherit recordsize $TESTSRCFS
log_must zfs inherit recordsize $TESTDSTFS

log_pass
