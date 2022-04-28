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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool create -O' should return an error with badly formed parameters.
#
# STRATEGY:
# 1. Create an array of parameters with '-O'
# 2. For each parameter in the array, execute 'zpool create -O'
# 3. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

set -A args "QuOta=none" "quota=non" "quota=abcd" "quota=0" "quota=" \
    "ResErVaTi0n=none" "reserV=none" "reservation=abcd" "reserv=" \
    "recorDSize=64k" "recordsize=32M" "recordsize=32768K" \
    "recordsize=256" "recsize=" "recsize=zero" "recordsize=0" \
    "mountPoint=/tmp/tmpfile$$" "mountpoint=non0" "mountpoint=" \
    "mountpoint=LEGACY" "mounpoint=none" \
    "sharenfs=ON" "ShareNFS=off" "sharenfs=sss" \
    "checkSUM=on" "checksum=SHA256" "chsum=off" "checksum=aaa" \
    "compression=of" "ComPression=lzjb" "compress=ON" "compress=a" \
    "atime=ON" "ATime=off" "atime=bbb" \
    "deviCes=on" "devices=OFF" "devices=aaa" \
    "exec=ON" "EXec=off" "exec=aaa" \
    "readonly=ON" "reADOnly=off" "rdonly=OFF" "rdonly=aaa" \
    "snapdIR=hidden" "snapdir=VISible" "snapdir=aaa" \
    "acltype=DIScard" "acltYPE=groupmask" "acltype=aaa" \
    "aclinherit=deny" "aclinHerit=secure" "aclinherit=aaa" \
    "type=volume" "type=snapshot" "type=filesystem" \
    "creation=aaa" "used=10K" "available=10K" \
    "referenced=10K" "compressratio=1.00x" \
    "version=0" "version=1.234" "version=10K" "version=-1" \
    "version=aaa" "version=999"
if is_freebsd; then
	args+=("jailed=ON" "JaiLed=off" "jailed=aaa")
else
	args+=("zoned=ON" "ZoNed=off" "zoned=aaa")
fi

log_assert "'zpool create -O' should return an error with badly formed parameters."

typeset -i i=0
while (( $i < ${#args[*]} )); do
	typeset arg=${args[i]}
	if is_freebsd; then
		# FreeBSD does not strictly validate share opts (yet).
		if [[ $arg == "sharenfs="* ]]; then
			((i = i + 1))
			continue
		fi
	fi
	log_mustnot zpool create -O $arg -f $TESTPOOL $DISKS
	((i = i + 1))
done

log_pass "'zpool create -O' should return an error with badly formed parameters."

