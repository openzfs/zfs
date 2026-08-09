#! /bin/ksh -p
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
# Copyright (c) 2023 by Pawel Jakub Dawidek
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_crossfs_block_cloning

save_tunable BCLONE_STRICT_PROPERTIES

function cleanup
{
	restore_tunable BCLONE_STRICT_PROPERTIES
	log_must zfs inherit recordsize $TESTSRCFS
	log_must zfs inherit compress $TESTSRCFS
	log_must zfs inherit recordsize $TESTDSTFS
	log_must zfs inherit compress $TESTDSTFS
}
log_onexit cleanup

log_assert "Verify block cloning across datasets with different recordsize properties"

# Disable strict property checking to allow cross-dataset cloning with different properties
log_must set_tunable32 BCLONE_STRICT_PROPERTIES 0

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

log_pass
