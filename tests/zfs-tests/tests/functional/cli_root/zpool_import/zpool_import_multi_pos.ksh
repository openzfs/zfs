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
# Verify comma-separated multi-pool import by name and GUID.
#
# STRATEGY:
# 1. Create several exported pools.
# 2. Import a subset by name and verify only those pools are imported.
# 3. Import remaining pools by GUID and by mixed name/GUID.
# 4. Import the same pool listed by both name and GUID in one request.
# 5. Verify shared options (-N, -R, -o) apply to every pool.
# 6. Ignore empty tokens and trim surrounding whitespace in the list.
# 7. Silently ignore duplicate identical pool names in the list.
# 8. Import destroyed pools with -D when every listed pool is destroyed.
# 9. From a shared cachefile, import only the listed pools and leave
#    other cachefile pools exported.
# 10. Verify zpool import -a still imports remaining pools.
#

verify_runnable "global"

POOLNAME="multi_imp"
typeset -i NPOOLS=4

function cleanup
{
	multi_import_destroy_pools $POOLNAME $NPOOLS
	[[ -d $ALTER_ROOT ]] && log_must rm -rf $ALTER_ROOT
	log_must rm -f $CPATH $CPATHBKP
	log_must rm -rf $DEVICE_DIR/*
	typeset -i i=0
	while (( i < MAX_NUM )); do
		log_must truncate -s $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		(( i += 1 ))
	done
}

log_assert "Comma-separated multi-pool import succeeds by name and GUID"
log_onexit cleanup

#
# Create four pools, record GUIDs, then export them.
#
typeset -a guids
typeset -i i=0
while (( i < NPOOLS )); do
	log_must zpool create ${POOLNAME}-$i \
	    ${DEVICE_DIR}/${DEVICE_FILE}$i
	guids[$i]=$(get_config ${POOLNAME}-$i pool_guid)
	guids[$i]=${guids[$i]// /}
	[[ -n ${guids[$i]} ]] || log_fail "failed to read guid for ${POOLNAME}-$i"
	log_must zpool export ${POOLNAME}-$i
	(( i += 1 ))
done

typeset guid0=${guids[0]}
typeset guid1=${guids[1]}
typeset guid2=${guids[2]}
typeset guid3=${guids[3]}

#
# Import a subset by name; other exported pools must remain exported.
#
typeset subset=$(multi_import_join_names $POOLNAME 0 1)
log_must zpool import -d $DEVICE_DIR -f $subset
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_mustnot poolexists ${POOLNAME}-3

#
# Import remaining pools by GUID.
#
log_must zpool import -d $DEVICE_DIR -f ${guid2},${guid3}
log_must poolexists ${POOLNAME}-2
log_must poolexists ${POOLNAME}-3

log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1 \
    ${POOLNAME}-2 ${POOLNAME}-3

#
# Mixed name and GUID in one request.
#
log_must zpool import -d $DEVICE_DIR -f ${POOLNAME}-0,${guid1}
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_mustnot poolexists ${POOLNAME}-3

log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# Same pool listed by both name and GUID must import once without error.
#
log_must zpool import -d $DEVICE_DIR -f ${POOLNAME}-0,${guid0}
log_must poolexists ${POOLNAME}-0
log_must zpool export ${POOLNAME}-0

#
# Shared options apply to every imported pool.
#
log_must zpool import -N -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot ismounted ${POOLNAME}-0
log_mustnot ismounted ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

log_must mkdir -p $ALTER_ROOT
log_must zpool import -R $ALTER_ROOT -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
[[ "$(get_pool_prop altroot ${POOLNAME}-0)" == "$ALTER_ROOT" ]] || \
    log_fail "altroot not set on ${POOLNAME}-0"
[[ "$(get_pool_prop altroot ${POOLNAME}-1)" == "$ALTER_ROOT" ]] || \
    log_fail "altroot not set on ${POOLNAME}-1"
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1
log_must rm -rf $ALTER_ROOT

log_must zpool import -o readonly=on -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
[[ "$(get_pool_prop readonly ${POOLNAME}-0)" == "on" ]] || \
    log_fail "readonly not set on ${POOLNAME}-0"
[[ "$(get_pool_prop readonly ${POOLNAME}-1)" == "on" ]] || \
    log_fail "readonly not set on ${POOLNAME}-1"
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# Empty tokens are ignored; surrounding whitespace is trimmed.
#
log_must zpool import -d $DEVICE_DIR -f \
    "${POOLNAME}-0,,${POOLNAME}-1"
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

log_must zpool import -d $DEVICE_DIR -f \
    " ${POOLNAME}-0 , ${POOLNAME}-1 "
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

log_must zpool import -d $DEVICE_DIR -f \
    ",${POOLNAME}-0,${POOLNAME}-1,"
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# Duplicate identical names are silently ignored.
#
log_must zpool import -d $DEVICE_DIR -f \
    ${POOLNAME}-0,${POOLNAME}-0
log_must poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0

#
# -D succeeds when every listed pool is destroyed.
#
log_must zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must zpool destroy ${POOLNAME}-0
log_must zpool destroy ${POOLNAME}-1
log_must zpool import -D -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_mustnot poolexists ${POOLNAME}-3
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# Multi-pool import from a shared cachefile imports only the listed
# pools even when other pools are present in the same cachefile.
#
log_must zpool import -a -d $DEVICE_DIR -f
log_must zpool set cachefile=$CPATH ${POOLNAME}-0
log_must zpool set cachefile=$CPATH ${POOLNAME}-1
log_must zpool set cachefile=$CPATH ${POOLNAME}-2
log_must zpool set cachefile=$CPATH ${POOLNAME}-3
log_must cp $CPATH $CPATHBKP
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1 \
    ${POOLNAME}-2 ${POOLNAME}-3
log_must zpool import -c $CPATHBKP -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_mustnot poolexists ${POOLNAME}-3
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# import -a still imports all remaining pools after the taskq err rewrite.
#
log_must zpool import -a -d $DEVICE_DIR -f
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_must poolexists ${POOLNAME}-2
log_must poolexists ${POOLNAME}-3

log_pass "Comma-separated multi-pool import succeeds by name and GUID"
