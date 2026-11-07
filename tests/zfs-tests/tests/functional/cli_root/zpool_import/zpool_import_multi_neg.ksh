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
# Verify error handling for comma-separated multi-pool import.
#
# STRATEGY:
# 1. Reject rename-import when multiple pools are requested.
# 2. Reject the request when one listed pool is already imported (name).
# 3. Reject the request when one listed pool is already imported (GUID).
# 4. Report missing requested pools while importing the rest.
# 5. Continue importing healthy pools when one pool's devices are missing.
# 6. Honor -D filtering for destroyed vs exported pools in a list.
# 7. When a pool is present in a cachefile but cannot be opened, import the
#    remaining pools and report the failed member (ip_err path).
# 8. A list of only empty tokens imports nothing and does not touch pools.
#

verify_runnable "global"

POOLNAME="multi_neg"
MISSING="multi_neg_missing"
HIDDEN_DIR=".hidden"
typeset -i NPOOLS=3
typeset ERRLOG=$TEST_BASE_DIR/multi_import_neg.err

function cleanup
{
	zinject -c all >/dev/null 2>&1
	multi_import_destroy_pools $POOLNAME $NPOOLS
	destroy_pool $MISSING
	log_must rm -f $ERRLOG $CPATH $CPATHBKP
	log_must rm -rf ${DEVICE_DIR}/${HIDDEN_DIR}
	log_must rm -rf $DEVICE_DIR/*
	typeset -i i=0
	while (( i < MAX_NUM )); do
		log_must truncate -s $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		(( i += 1 ))
	done
}

log_assert "Comma-separated multi-pool import handles error conditions"
log_onexit cleanup

multi_import_create_exported_pools $POOLNAME $NPOOLS

#
# Rename is not allowed with a multi-pool list.
#
log_mustnot_expect "cannot do a rename-import when importing multiple pools" \
    zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1) ${POOLNAME}-new
log_mustnot poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-new

#
# One listed pool already imported (by name).
#
log_must zpool import -d $DEVICE_DIR -f ${POOLNAME}-0
log_mustnot_expect "one or more pools already imported" \
    zpool import -d $DEVICE_DIR -f $(multi_import_join_names $POOLNAME 0 2)
log_must poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2

#
# One listed pool already imported (by GUID).
#
typeset guid0=$(get_config ${POOLNAME}-0 pool_guid)
guid0=${guid0// /}
[[ -n $guid0 ]] || log_fail "failed to read guid for ${POOLNAME}-0"
log_mustnot_expect "one or more pools already imported" \
    zpool import -d $DEVICE_DIR -f ${guid0},${POOLNAME}-1
log_must poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_must zpool export ${POOLNAME}-0

#
# Missing requested pool: healthy members import, overall command fails.
#
zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1),$MISSING >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure importing missing pool"
log_must eval "grep -q 'Not all pools were imported' $ERRLOG"
log_must eval "grep -q \"pool '$MISSING' not imported\" $ERRLOG"
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

#
# Missing-GUID form of the not-imported report.
#
typeset bogous_guid=1234567890123456789
zpool import -d $DEVICE_DIR -f ${POOLNAME}-0,$bogous_guid >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure importing missing guid"
log_must eval "grep -q 'Not all pools were imported' $ERRLOG"
log_must eval "grep -q \"pool '$bogous_guid' not imported\" $ERRLOG"
log_must poolexists ${POOLNAME}-0
log_must zpool export ${POOLNAME}-0

#
# One pool's device is unavailable; others still import.
#
if [[ ! -d ${DEVICE_DIR}/${HIDDEN_DIR} ]]; then
	log_must mkdir ${DEVICE_DIR}/${HIDDEN_DIR}
fi
log_must mv ${DEVICE_DIR}/${DEVICE_FILE}2 ${DEVICE_DIR}/${HIDDEN_DIR}
zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 2) >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure with missing device"
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_must eval "grep -q 'Not all pools were imported' $ERRLOG"
log_must eval "grep -q \"pool '${POOLNAME}-2' not imported\" $ERRLOG"
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1
log_must mv ${DEVICE_DIR}/${HIDDEN_DIR}/${DEVICE_FILE}2 ${DEVICE_DIR}

#
# Destroyed vs exported filtering with a multi-pool list.
#
log_must zpool import -d $DEVICE_DIR -f ${POOLNAME}-2
log_must zpool destroy ${POOLNAME}-2
zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 2) >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure for destroyed pool without -D"
log_must poolexists ${POOLNAME}-0
log_must poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
log_must eval "grep -q \"pool '${POOLNAME}-2' not imported\" $ERRLOG"
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1

zpool import -D -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 2) >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure for exported pools with -D"
log_must poolexists ${POOLNAME}-2
log_mustnot poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_must eval "grep -q 'Not all pools were imported' $ERRLOG"
log_must zpool export ${POOLNAME}-2

#
# Pool discovered via cachefile but import fails (device wiped): other
# listed pools still import and the failure is reported.
#
log_must zpool import -d $DEVICE_DIR -f \
    $(multi_import_join_names $POOLNAME 0 1)
log_must zpool set cachefile=$CPATH ${POOLNAME}-0
log_must zpool set cachefile=$CPATH ${POOLNAME}-1
log_must cp $CPATH $CPATHBKP
log_must zpool export ${POOLNAME}-0 ${POOLNAME}-1
log_must rm -f ${DEVICE_DIR}/${DEVICE_FILE}1
log_must truncate -s $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}1
zpool import -c $CPATHBKP -f \
    $(multi_import_join_names $POOLNAME 0 1) >$ERRLOG 2>&1
(( $? != 0 )) || log_fail "expected failure when cachefile pool cannot open"
log_must poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_must eval "grep -q 'Not all pools were imported' $ERRLOG"
log_must eval "grep -q \"pool '${POOLNAME}-1' not imported\" $ERRLOG"
log_must zpool export ${POOLNAME}-0

#
# A comma-list of only empty tokens matches no pools and imports nothing.
# (validate_pool_list still builds a non-NULL pool_list because of ','.)
#
zpool import -d $DEVICE_DIR -f "," >$ERRLOG 2>&1
typeset empty_tok_status=$?
log_mustnot poolexists ${POOLNAME}-0
log_mustnot poolexists ${POOLNAME}-1
log_mustnot poolexists ${POOLNAME}-2
(( empty_tok_status == 0 )) || log_fail \
    "empty-token list unexpectedly failed with status $empty_tok_status"

log_pass "Comma-separated multi-pool import handles error conditions"
