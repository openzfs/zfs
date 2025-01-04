#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2023 by Findity AB
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that receiving a raw encrypted stream, with a FREEOBJECTS
# removing all existing objects in a block followed by an OBJECT write
# to the same block, does not result in a panic.
#
# Strategy:
# 1. Create a new encrypted filesystem
# 2. Create file f1 as the first object in some block (here object 128)
# 3. Take snapshot A
# 4. Create file f2 as the second object in the same block (here object 129)
# 5. Delete f1
# 6. Take snapshot B
# 7. Receive a full raw encrypted send of A
# 8. Receive an incremental raw send of B
#
verify_runnable "both"

function create_object_with_num
{
	file=$1
	num=$2

	tries=100
	for ((i=0; i<$tries; i++)); do
		touch $file
		onum=$(ls -li $file | awk '{print $1}')

		if [[ $onum -ne $num ]] ; then
			rm -f $file
		else
			break
		fi
	done
	if [[ $i -eq $tries ]]; then
		log_fail "Failed to create object with number $num"
	fi
}

log_assert "FREEOBJECTS followed by OBJECT in encrypted stream does not crash"

sendds=sendencfods
recvds=recvencfods
keyfile=/$POOL/keyencfods
f1=/$POOL/$sendds/f1
f2=/$POOL/$sendds/f2

log_must eval "echo 'password' > $keyfile"

#
# xattr=sa and dnodesize=legacy for sequential object numbers, see
# note in send_freeobjects.ksh.
#
log_must zfs create -o xattr=sa -o dnodesize=legacy -o encryption=on \
	-o keyformat=passphrase -o keylocation=file://$keyfile $POOL/$sendds

create_object_with_num $f1 128
log_must zfs snap $POOL/$sendds@A
create_object_with_num $f2 129
log_must rm $f1
log_must zfs snap $POOL/$sendds@B

log_must eval "zfs send -w $POOL/$sendds@A | zfs recv $POOL/$recvds"
log_must eval "zfs send -w -i $POOL/$sendds@A $POOL/$sendds@B |" \
	"zfs recv $POOL/$recvds"

log_pass "FREEOBJECTS followed by OBJECT in encrypted stream did not crash"
