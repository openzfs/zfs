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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify FSType-specific option works well with legacy mount.
#
# STRATEGY:
#	1. Set up FSType-specific options and expected keywords array.
#	2. Create a test ZFS file system and set mountpoint=legacy.
#	3. Mount ZFS test filesystem with specific options.
#	4. Verify the filesystem was mounted with specific option.
#	5. Loop check all the options.
#

verify_runnable "both"

function cleanup
{
	ismounted $tmpmnt && log_must umount $tmpmnt
	[[ -d $tmpmnt ]] && log_must rm -rf $tmpmnt
	[[ -n $oldmpt ]] && log_must zfs set mountpoint=$oldmpt $testfs
	! ismounted $oldmpt && log_must zfs mount $testfs
}

log_assert "With legacy mount, FSType-specific option works well."
log_onexit cleanup

#
#  /mnt on pool/fs read/write/setuid/devices/noexec/xattr/atime/dev=2d9009e
#
#	FSType-				FSType-
#	specific	Keyword		specific	Keyword
#	option				option
#
if is_linux; then
	set -A args \
	"nodev"		"dev"	\
	"noexec"	"exec"	\
	"ro"		"rw"	\
	"nosuid"	"suid"	\
	"xattr"		"noxattr"	\
	"atime"		"noatime"

	# Only older kernels support non-blocking mandatory locks
	if [[ $(linux_version) -lt $(linux_version "4.4") ]]; then
		args+=("mand" "nomand")
	fi
elif is_freebsd; then
	# 'xattr' and 'devices' are not supported on FreeBSD
	# Perhaps more options need to be added.
	set -A args \
	"noexec"	"exec"	\
	"ro"		"rw"	\
	"nosuid"	"suid"	\
	"atime"		"noatime"
else
	set -A args \
	"devices"	"/devices/"	"nodevices"	"/nodevices/"	\
	"exec"		"/exec/"	"noexec"	"/noexec/"	\
	"nbmand"	"/nbmand/"	"nonbmand"	"/nonbmand/"	\
	"ro"		"read only"	"rw"		"read/write"	\
	"setuid"	"/setuid/"	"nosetuid"	"/nosetuid/"	\
	"xattr"		"/xattr/"	"noxattr"	"/noxattr/"	\
	"atime"		"/atime/"	"noatime"	"/noatime/"
fi

tmpmnt=/tmpmnt.$$
[[ -d $tmpmnt ]] && rm -rf $tmpmnt
testfs=$TESTPOOL/$TESTFS
log_must mkdir $tmpmnt
oldmpt=$(get_prop mountpoint $testfs)
log_must zfs set mountpoint=legacy $testfs

typeset i=0
while ((i < ${#args[@]})); do
	if is_linux || is_freebsd; then
		log_must mount -t zfs -o ${args[$i]} $testfs $tmpmnt
		
		msg=$(mount | grep "$tmpmnt ")

		if ! echo $msg | grep -q "${args[((i))]}"; then
			if echo $msg | grep -q "${args[((i-1))]}"; then
				log_fail "Expected option: ${args[((i))]} \n" \
					 "Real option: $msg"
			fi
		fi

		log_must umount $tmpmnt
		((i += 1))
	else
		log_must mount -F zfs -o ${args[$i]} $testfs $tmpmnt

		msg=$(mount | grep "^$tmpmnt ")

		# In LZ, a user with all zone privileges can never "devices"
		if ! is_global_zone && [[ ${args[$i]} == devices ]] ; then
			args[((i+1))]="/nodevices/"
		fi

		if ! echo $msg | grep -q "${args[((i+1))]}"; then
			log_fail "Expected option: ${args[((i+1))]} \n" \
				 "Real option: $msg"
		fi


		log_must umount $tmpmnt
		((i += 2))
	fi
done

log_pass "With legacy mount, FSType-specific option works well passed."
