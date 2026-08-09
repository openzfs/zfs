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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg

#
# DESCRIPTION:
# 'zfs create' should return an error with badly formed parameters.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zfs create'
# 3. Verify an error is returned.
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup

set -A args "ab" "-?" "-cV" "-Vc" "-c -V" "c" "V" "--c" "-e" "-s" \
    "-blah" "-cV 12k" "-s -cV 1P" "-sc" "-Vs 5g" "-o" "--o" "-O" "--O" \
    "-o QuOta=none" "-o quota=non" "-o quota=abcd" "-o quota=0" "-o quota=" \
    "-o ResErVaTi0n=none" "-o reserV=none" "-o reservation=abcd" "-o reserv=" \
    "-o recorDSize=64k" "-o recordsize=32768K" "-o recordsize=32M" \
    "-o recordsize=256" "-o recsize=" "-o recsize=zero" "-o recordsize=0" \
    "-o mountPoint=/tmp/tmpfile$$" "-o mountpoint=non0" "-o mountpoint=" \
    "-o mountpoint=LEGACY" "-o mounpoint=none" \
    "-o sharenfs=ON" "-o ShareNFS=off" "-o sharenfs=sss" \
    "-o checkSUM=on" "-o checksum=SHA256" "-o chsum=off" "-o checksum=aaa" \
    "-o checkSUM=on -V $VOLSIZE" "-o checksum=SHA256 -V $VOLSIZE" \
    "-o chsum=off -V $VOLSIZE" "-o checksum=aaa -V $VOLSIZE" \
    "-o compression=of" "-o ComPression=lzjb" "-o compress=ON" "-o compress=a" \
    "-o compression=of -V $VOLSIZE" "-o ComPression=lzjb -V $VOLSIZE" \
    "-o compress=ON -V $VOLSIZE" "-o compress=a -V $VOLSIZE" \
    "-o atime=ON" "-o ATime=off" "-o atime=bbb" \
    "-o deviCes=on" "-o devices=OFF" "-o devices=aaa" \
    "-o exec=ON" "-o EXec=off" "-o exec=aaa" \
    "-o readonly=ON" "-o reADOnly=off" "-o rdonly=OFF" "-o rdonly=aaa" \
    "-o readonly=ON -V $VOLSIZE" "-o reADOnly=off -V $VOLSIZE" \
    "-o rdonly=OFF -V $VOLSIZE" "-o rdonly=aaa -V $VOLSIZE" \
    "-o snapdIR=hidden" "-o snapdir=VISible" "-o snapdir=aaa" \
    "-o aclmode=DIScard" "-o aclmODE=groupmask" "-o aclmode=aaa" \
    "-o aclinherit=deny" "-o aclinHerit=secure" "-o aclinherit=aaa" \
    "-o type=volume" "-o type=snapshot" "-o type=filesystem" \
    "-o type=volume -V $VOLSIZE" "-o type=snapshot -V $VOLSIZE" \
    "-o type=filesystem -V $VOLSIZE" \
    "-o creation=aaa" "-o creation=aaa -V $VOLSIZE" \
    "-o used=10K" "-o used=10K -V $VOLSIZE" \
    "-o available=10K" "-o available=10K -V $VOLSIZE" \
    "-o referenced=10K" "-o referenced=10K -V $VOLSIZE" \
    "-o compressratio=1.00x" "-o compressratio=1.00x -V $VOLSIZE" \
    "-o version=0" "-o version=1.234" "-o version=10K" "-o version=-1" \
    "-o version=aaa" "-o version=999"
if is_freebsd; then
	args+=("-o jailed=ON" "-o JaiLed=off" "-o jailed=aaa")
else
	args+=("-o zoned=ON" "-o ZoNed=off" "-o zoned=aaa")
fi

log_assert "'zfs create' should return an error with badly-formed parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	typeset arg=${args[i]}
	if is_freebsd; then
		# FreeBSD does not strictly validate share options (yet).
		if [[ "$arg" == "-o sharenfs="* ]]; then
			((i = i + 1))
			continue
		fi
	fi
	log_mustnot zfs create $arg $TESTPOOL/$TESTFS1
	log_mustnot zfs create -p $arg $TESTPOOL/$TESTFS1
	((i = i + 1))
done

log_pass "'zfs create' with badly formed parameters failed as expected."
