#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The parity-epoch table of a raidz vdev is stored by the setter, with
#	its copy in the vdev ZAP and the feature that keep it, and read back
#	from disk.  The config of a raidz vdev that is created, with the pool
#	or by vdev add, must not carry one: it would be used without either.
#	A config read from disk does carry the table that the setter stored,
#	also where it is parsed outside the load of the pool, as the root
#	pool import of FreeBSD does with the labels of the boot devices.
#
# STRATEGY:
#	1. Create a pool whose raidz vdev config carries a table: refused.
#	2. Create the same pool without the table.
#	3. Add a raidz vdev whose config carries a table: refused, and the
#	   pool keeps its one top-level vdev.
#	4. Parse the labels of the pool as the root pool import does, before
#	   and after zhack stores a table: both parse, and the second puts
#	   the table of the labels in force.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_config.$$
typeset -r POOL=${TESTPOOL}_config
typeset -i WIDTH=5
typeset out

function cleanup
{
	rm -rf $BASEDIR
}

log_onexit cleanup
log_assert "the config of a new raidz vdev cannot carry a parity-epoch table"

log_must mkdir -p $BASEDIR

out=$(raidz_epoch_test forge $BASEDIR $POOL $WIDTH 1 create) ||
    log_fail "forge create did not complete: $out"
log_note "$out"
[[ $out == *"forge create rc=22 "* ]] ||
    log_fail "a pool was created with a table in its raidz vdev config"

log_must raidz_epoch_test create $BASEDIR $POOL $WIDTH 1

out=$(raidz_epoch_test forge $BASEDIR $POOL $WIDTH 2 add) ||
    log_fail "forge add did not complete: $out"
log_note "$out"
[[ $out == *"forge add rc=22 top=1"* ]] ||
    log_fail "a raidz vdev with a table in its config was added"

out=$(raidz_epoch_test rootparse $BASEDIR $POOL) ||
    log_fail "rootparse did not complete: $out"
log_note "$out"
[[ $out == *"rootparse rc=0 label=0 table=0"* ]] ||
    log_fail "the labels of a pool without a table were not parsed"
out=$(zhack -d $BASEDIR raidz_epochs $POOL 0 0:$WIDTH:1 next:$WIDTH:2) ||
    log_fail "zhack did not store a table: $out"
log_note "$out"
out=$(raidz_epoch_test rootparse $BASEDIR $POOL) ||
    log_fail "rootparse did not complete: $out"
log_note "$out"
[[ $out == *"rootparse rc=0 label=6 table=2"* ]] ||
    log_fail "the labels of a pool whose table zhack stored were refused"

log_pass "the config of a new raidz vdev cannot carry a parity-epoch table"
