#!/bin/ksh

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
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify FREEOBJECTS record frees sequential objects (See
# https://github.com/openzfs/zfs/issues/6694)
#
# Strategy:
# 1. Create three files with sequential object numbers, f1 f2 and f3
# 2. Delete f2
# 3. Take snapshot A
# 4. Delete f3
# 5. Take snapshot B
# 6. Receive a full send of A
# 7. Receive an incremental send of B
# 8. Fail test if f3 exists on received snapshot B
#

verify_runnable "both"

log_assert "Verify FREEOBJECTS record frees sequential objects"

sendds=sendfo
recvds=recvfo
f1=/$POOL/$sendds/f1
f2=/$POOL/$sendds/f2
f3=/$POOL/$sendds/f3

#
# We need to set xattr=sa and dnodesize=legacy to guarantee sequential
# object numbers for this test. Otherwise, if we used directory-based
# xattrs, SELinux extended attributes might consume intervening object
# numbers.
#
log_must zfs create -o xattr=sa -o dnodesize=legacy $POOL/$sendds

tries=100
for ((i=0; i<$tries; i++)); do
	touch $f1 $f2 $f3
	o1=$(ls -li $f1 | awk '{print $1}')
	o2=$(ls -li $f2 | awk '{print $1}')
	o3=$(ls -li $f3 | awk '{print $1}')

	if [[ $o2 -ne $(( $o1 + 1 )) ]] || [[ $o3 -ne $(( $o2 + 1 )) ]]; then
		rm -f $f1 $f2 $f3
	else
		break
	fi
done

if [[ $i -eq $tries ]]; then
	log_fail "Failed to create three sequential objects"
fi

log_must rm $f2
log_must zfs snap $POOL/$sendds@A
log_must rm $f3
log_must zfs snap $POOL/$sendds@B
log_must eval "zfs send $POOL/$sendds@A | zfs recv $POOL/$recvds"
log_must eval "zfs send -i $POOL/$sendds@A $POOL/$sendds@B |" \
	"zfs recv $POOL/$recvds"
log_mustnot zdb $POOL/$recvds@B $o3
log_pass "Verify FREEOBJECTS record frees sequential objects"
