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
#	When a parity-epoch table makes zpool import fail, the message says
#	so, instead of reporting a read-only device or an unsupported
#	version, and points at a read-only import when one is possible.
#
# STRATEGY:
#	1. Give a raidz1 pool a table whose second epoch it has reached, in
#	   the labels and the MOS config only.  Require zpool import to fail
#	   and to name '-o readonly=on', and a read-only import to succeed.
#	2. Store a table the usual way on another pool, and set the rescue
#	   tunable.  Require the same of zpool import.
#	3. Expand a third pool, then store a table in its vdev ZAP with
#	   zhack -f.  Require zpool import to fail and to name the expansion
#	   history.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_import.$$
typeset -r POOL=${TESTPOOL}_pe_import
typeset -r RDONLY="readonly=on"
typeset -r EXPANDED="parity-epoch table and expansion history"
typeset dir

function cleanup
{
	set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
	poolexists $POOL && destroy_pool $POOL
	rm -rf $BASEDIR
}

# new_pool <dir> <children>: a raidz1 pool on files, exported.
function new_pool
{
	typeset d=$1
	typeset -i n=$2 i

	log_must mkdir -p $d
	for (( i = 0; i < n; i++ )); do
		log_must truncate -s 256M $d/v$i
	done
	log_must zpool create -f -o cachefile=none -o ashift=12 $POOL \
	    raidz1 $(for (( i = 0; i < n; i++ )); do print $d/v$i; done)
	sync_pool $POOL
}

log_onexit cleanup
log_assert "zpool import names the parity-epoch table that refuses it"

dir=$BASEDIR/config
new_pool $dir 4
log_must zpool export $POOL
log_must raidz_epoch_test label $dir $POOL now
log_mustnot_expect "$RDONLY" zpool import -d $dir $POOL
log_must zpool import -o readonly=on -d $dir $POOL
log_must zpool export $POOL

dir=$BASEDIR/tunable
new_pool $dir 4
log_must zpool export $POOL
log_must zhack -d $dir raidz_epochs $POOL 0 0:4:1 next:4:2
log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 1
log_mustnot_expect "$RDONLY" zpool import -d $dir $POOL
log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
log_must zpool import -d $dir $POOL
log_must zpool export $POOL

dir=$BASEDIR/expanded
new_pool $dir 4
log_must truncate -s 256M $dir/v4
log_must zpool attach -w $POOL raidz1-0 $dir/v4
log_must zpool export $POOL
log_must zhack -d $dir raidz_epochs -f $POOL 0 0:5:1
log_mustnot_expect "$EXPANDED" zpool import -d $dir $POOL

log_pass "zpool import names the parity-epoch table that refuses it"
