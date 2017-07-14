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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_send/zfs_send.cfg

#
# DESCRIPTION:
#	Verify 'zfs send' can generate valid streams with a property setup.
#
# STRATEGY:
#	1. Setup property for filesystem
#	2. Fill in some data into filesystem
#	3. Create a full send streams
#	4. Receive the send stream
#	5. Verify the receive result
#

verify_runnable "both"

function cleanup
{
	snapexists $snap && \
		log_must zfs destroy $snap

	datasetexists $ctr && \
		log_must zfs destroy -r $ctr

	[[ -e $origfile ]] && \
		log_must rm -f $origfile

	[[ -e $stream ]] && \
		log_must rm -f $stream
}

function do_testing # <prop> <prop_value>
{
	typeset property=$1
	typeset prop_val=$2

	log_must zfs set $property=$prop_val $fs
	file_write -o create -f $origfile -b $BLOCK_SIZE -c $WRITE_COUNT
	log_must zfs snapshot $snap
	zfs send $snap > $stream
	(( $? != 0 )) && \
		log_fail "'zfs send' fails to create send streams."
	zfs receive -d $ctr <$stream
	(( $? != 0 )) && \
		log_fail "'zfs receive' fails to receive send streams."

	#verify receive result
	! datasetexists $rstfs && \
		log_fail "'zfs receive' fails to restore $rstfs"
	! snapexists $rstfssnap && \
		log_fail "'zfs receive' fails to restore $rstfssnap"
	if [[ ! -e $rstfile ]] || [[ ! -e $rstsnapfile ]]; then
		log_fail " Data lost after receiving stream"
	fi

	compare_cksum $origfile $rstfile
	compare_cksum $origsnapfile $rstsnapfile

	#Destroy datasets and stream for next testing
	log_must zfs destroy $snap
	if is_global_zone ; then
		log_must zfs destroy -r $rstfs
	else
		log_must zfs destroy -r $ds_path
	fi
	log_must rm -f $stream
}

log_assert "Verify 'zfs send' generates valid streams with a property setup"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap=$fs@$TESTSNAP
ctr=$TESTPOOL/$TESTCTR
if is_global_zone; then
	rstfs=$ctr/$TESTFS
else
	ds_path=$ctr/${ZONE_CTR}0
	rstfs=$ds_path/$TESTFS
fi
rstfssnap=$rstfs@$TESTSNAP
snapdir=".zfs/snapshot/$TESTSNAP"
origfile=$TESTDIR/$TESTFILE1
rstfile=/$rstfs/$TESTFILE1
origsnapfile=$TESTDIR/$snapdir/$TESTFILE1
rstsnapfile=/$rstfs/$snapdir/$TESTFILE1
stream=/var/tmp/streamfile.$$

set -A props "compression" "checksum" "recordsize"
set -A propval "on lzjb" "on fletcher2 fletcher4 sha256" \
	"512 1k 4k 8k 16k 32k 64k 128k"

#Create a dataset to receive the send stream
log_must zfs create $ctr

typeset -i i=0
while (( i < ${#props[*]} ))
do
	for value in ${propval[i]}
	do
		do_testing ${props[i]} $value
	done

	(( i = i + 1 ))
done

log_pass "'zfs send' generates streams with a property setup as expected."
