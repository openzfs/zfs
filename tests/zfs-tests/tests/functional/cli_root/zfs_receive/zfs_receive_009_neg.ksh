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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs receive' fails with bad options, missing argument or too many
#	arguments.
#
# STRATEGY:
#	1. Set a array of illegal arguments
#	2. Execute 'zfs receive' with illegal arguments
#	3. Verify the command should be failed
#

verify_runnable "both"

function cleanup
{
	typeset ds

	snapexists $snap && destroy_dataset $snap

	for ds in $ctr1 $ctr2 $fs1; do
		datasetexists $ds && destroy_dataset $ds -rf
	done
	if [[ -d $TESTDIR2 ]]; then
		rm -rf $TESTDIR2
	fi
}

log_assert "Verify 'zfs receive' fails with bad option, missing or too many arguments"
log_onexit cleanup

set -A badopts "v" "n" "F" "d" "-V" "-N" "-f" "-D" "-VNfD" "-vNFd" "-vnFD" "-dVnF" \
		"-vvvNfd" "-blah" "-12345" "-?" "-*" "-%"
set -A validopts "" "-v" "-n" "-F" "-vn" "-nF" "-vnF" "-vd" "-nd" "-Fd" "-vnFd"

ctr1=$TESTPOOL/$TESTCTR1
ctr2=$TESTPOOL/$TESTCTR2
fs1=$TESTPOOL/$TESTFS1
fs2=$TESTPOOL/$TESTFS2
fs3=$TESTPOOL/$TESTFS3
snap=$TESTPOOL/$TESTFS@$TESTSNAP
bkup=$TESTDIR2/bkup.$$

# Preparations for negative testing
for ctr in $ctr1 $ctr2; do
	log_must zfs create $ctr
done
if [[ -d $TESTDIR2 ]]; then
	rm -rf $TESTDIR2
fi
log_must zfs create -o mountpoint=$TESTDIR2 $fs1
log_must zfs snapshot $snap
log_must eval "zfs send $snap > $bkup"

#Testing zfs receive fails with input from terminal
log_mustnot eval "zfs recv $fs3 </dev/console"

# Testing with missing argument and too many arguments
typeset -i i=0
while (( i < ${#validopts[*]} )); do
	log_mustnot eval "zfs recv < $bkup"

	if echo ${validopts[i]} | grep -q "d"; then
		log_mustnot eval "zfs recv ${validopts[i]} $ctr1 $ctr2 < $bkup"
	else
		log_mustnot eval "zfs recv ${validopts[i]} $fs2 $fs3 < $bkup"
	fi

	(( i += 1 ))
done

# Testing with bad options
i=0
while (( i < ${#badopts[*]} ))
do
	log_mustnot eval "zfs recv ${badopts[i]} $ctr1 < $bkup"
	log_mustnot eval "zfs recv ${badopts[i]} $fs2 < $bkup"

	(( i = i + 1 ))
done

log_pass "'zfs receive' as expected with bad options, missing or too many arguments."
