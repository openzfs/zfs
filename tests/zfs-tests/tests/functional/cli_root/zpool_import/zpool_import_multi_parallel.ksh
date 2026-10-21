#!/bin/ksh -p
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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
# Verify comma-separated multi-pool import runs imports in parallel.
#
# STRATEGY:
# 1. Create several pools with import delay injectors.
# 2. Measure sequential single-pool imports as a baseline.
# 3. Import the same number of pools with one comma-separated request.
# 4. Assert the multi-import completes faster than sequential imports.
#

verify_runnable "global"

export MAX_NUM=8
POOLNAME="multi_par"
typeset -i NPOOLS=4
VDEVSIZE=$((512 * 1024 * 1024))

function cleanup
{
	zinject -c all
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must set_tunable64 METASLAB_DEBUG_LOAD 0

	multi_import_destroy_pools $POOLNAME $MAX_NUM
	increase_device_sizes 0
	increase_device_sizes $FILE_SIZE
}

log_assert "Comma-separated multi-pool import occurs in parallel"
log_onexit cleanup

increase_device_sizes $VDEVSIZE

log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must set_tunable64 METASLAB_DEBUG_LOAD 1

typeset -i i=0
while (( i < NPOOLS * 2 )); do
	log_must zpool create ${POOLNAME}-$i \
	    ${DEVICE_DIR}/${DEVICE_FILE}$i
	log_must zpool export ${POOLNAME}-$i
	log_must zinject -P import -s 12 ${POOLNAME}-$i
	(( i += 1 ))
done

#
# Sequential baseline for the first half of the pools.
#
SECONDS=0
i=0
while (( i < NPOOLS )); do
	log_must zpool import -d $DEVICE_DIR -f ${POOLNAME}-$i
	(( i += 1 ))
done
typeset sequential_time=$SECONDS
log_note "sequentially imported $NPOOLS pools in $sequential_time seconds"

#
# Comma-separated multi-import for the second half.
#
SECONDS=0
log_must zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME $NPOOLS $((NPOOLS * 2 - 1)))
typeset parallel_time=$SECONDS
log_note "multi-imported $NPOOLS pools in $parallel_time seconds"

i=$NPOOLS
while (( i < NPOOLS * 2 )); do
	log_must poolexists ${POOLNAME}-$i
	(( i += 1 ))
done

log_must test $parallel_time -lt $(($sequential_time * 3 / 4))

log_pass "Comma-separated multi-pool import occurs in parallel"
