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
	log_must zfs inherit compress $TESTSRCFS
	log_must zfs inherit compress $TESTDSTFS
}
log_onexit cleanup

log_assert "Verify block cloning across datasets with different compression properties"

# Disable strict property checking to allow cross-dataset cloning with different properties
log_must set_tunable32 BCLONE_STRICT_PROPERTIES 0

for srcprop in "${compress_prop_vals[@]}"; do
    for dstprop in "${compress_prop_vals[@]}"; do
        if [[ $srcprop == $dstprop ]]; then
            continue
        fi
        log_must zfs set compress=$srcprop $TESTSRCFS
        log_must zfs set compress=$dstprop $TESTDSTFS
        # 15*8=120, which is greater than 113, so we are sure the data won't
        # be embedded into BP.
        # 32767*8=262136, which is larger than a single default recordsize of
        # 131072.
        FILESIZE=$(random_int_between 15 32767)
        FILESIZE=$((FILESIZE * 8))
        bclone_test text $FILESIZE false $TESTSRCDIR $TESTDSTDIR
    done
done

log_pass
