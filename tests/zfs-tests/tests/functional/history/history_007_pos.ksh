#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Verify command history moves with pool while pool being migrated
#
# STRATEGY:
#	1. Import uniform platform and cross platform pools
#	2. Contract the command history of the imported pool
#	3. Compare imported history log with the previous log.
#

verify_runnable "global"

function cleanup
{
	poolexists $migratedpoolname &&  \
		log_must zpool destroy -f $migratedpoolname

	rm -rf $import_dir
}

log_assert "Verify command history moves with migrated pool."
log_onexit cleanup

tst_dir=$STF_SUITE/tests/functional/history
import_dir=$TESTDIR/importdir.$$
migrated_cmds_f=$import_dir/migrated_history.$$
migratedpoolname=$MIGRATEDPOOLNAME
typeset -i RET=1
typeset -i linenum=0

[[ ! -d $import_dir ]] && log_must mkdir -p $import_dir

# We test the migrations on both uniform platform and cross platform
for arch in "i386" "sparc"; do
	log_must cp $tst_dir/${arch}.orig_history.txt $import_dir
	orig_cmds_f=$import_dir/${arch}.orig_history.txt
	# remove blank line
	orig_cmds_f1=$import_dir/${arch}.orig_history_1.txt
	grep -v "^$" $orig_cmds_f > $orig_cmds_f1

	log_must cp $tst_dir/${arch}.migratedpool.DAT.Z $import_dir
	log_must uncompress -f $import_dir/${arch}.migratedpool.DAT.Z

	# destroy the pool with same name, so that import operation succeeds.
	poolexists $migratedpoolname && \
	    log_must zpool destroy -f $migratedpoolname

	log_must zpool import -d $import_dir $migratedpoolname
	log_must eval "TZ=$TIMEZONE zpool history $migratedpoolname | grep -v \"^\$\" >$migrated_cmds_f"

	# The migrated history file should differ with original history file on
	# two commands -- 'export' and 'import', which are included in migrated
	# history file but not in original history file. so, check the two
	# commands firstly in migrated history file and then delete them, and
	# then compare this filtered file with the original history file. They
	# should be identical at this time.
	for subcmd in "export" "import"; do
		grep -q "$subcmd" $migrated_cmds_f ||
			log_fail "zpool $subcmd is not logged for" \
			    "the imported pool $migratedpoolname."
	done

	tmpfile=$import_dir/cmds_tmp.$$
	linenum=$(wc -l < $migrated_cmds_f)
	(( linenum = linenum - 2 ))
	head -n $linenum $migrated_cmds_f > $tmpfile
	log_must diff $tmpfile $orig_cmds_f1

	# cleanup for next loop testing
	log_must zpool destroy -f $migratedpoolname
	log_must rm -f $(ls $import_dir)
done

log_pass "Verify command history moves with migrated pool."
