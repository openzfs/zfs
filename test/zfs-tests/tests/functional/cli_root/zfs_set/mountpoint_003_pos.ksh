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
	ismounted $tmpmnt && log_must $UMOUNT $tmpmnt
	[[ -d $tmpmnt ]] && log_must $RM -rf $tmpmnt
	[[ -n $oldmpt ]] && log_must $ZFS set mountpoint=$oldmpt $testfs
	! ismounted $oldmpt && log_must $ZFS mount $testfs
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
set -A args \
	"devices"	"/devices/"	"nodevices"	"/nodevices/"	\
	"exec"		"/exec/"	"noexec"	"/noexec/"	\
	"nbmand"	"/nbmand/"	"nonbmand"	"/nonbmand/"	\
	"ro"		"read only"	"rw"		"read/write"	\
	"setuid"	"/setuid/"	"nosetuid"	"/nosetuid/"	\
	"xattr"		"/xattr/"	"noxattr"	"/noxattr/"	\
	"atime"		"/atime/"	"noatime"	"/noatime/"

tmpmnt=/tmpmnt.$$
[[ -d $tmpmnt ]] && $RM -rf $tmpmnt
testfs=$TESTPOOL/$TESTFS
log_must $MKDIR $tmpmnt
oldmpt=$(get_prop mountpoint $testfs)
log_must $ZFS set mountpoint=legacy $testfs

typeset i=0
while ((i < ${#args[@]})); do
	if [[ -n "$LINUX" ]]; then
		# Mount options differ slightly on Linux, so translate
		case ${args[$i]} in
			devices)	args[$i]="dev"		;;
			/devices/)	args[$i]=",dev,"	;;
			nodevices)	args[$i]="nodev"	;;
			/nodevices)	args[$i]=",nodev,"	;;
			nbmand)		args[$i]="mand"		;;
			nonbmand)	args[$i]="nomand"	;;
			setuid)		args[$i]="suid"		;;
			nosetuid)	args[$i]="nosuid"	;;
		esac
		log_must $MOUNT -t zfs -o ${args[$i]} $testfs $tmpmnt
	else
		log_must $MOUNT -F zfs -o ${args[$i]} $testfs $tmpmnt
	fi
	msg=$($MOUNT | $GREP "^$tmpmnt ")

	# In LZ, a user with all zone privileges can never with "devices"
	if ! is_global_zone && [[ ${args[$i]} == devices ]] ; then
		if [[ -n "$LINUX" ]]; then
			args[((i+1))]=",nodev,"
		else
			args[((i+1))]="/nodevices/"
		fi
	fi

	# On Linux 'dev' is a default option, and if specified, doesn't
	# show up in the mount list. So just skip the test - if we got
	# this far, the mount succeeded!
	if [[ ${args[((i+1))]} == ",dev," ]]; then
		if [[ -n "$LINUX" ]]; then
			mknod /tmpmnt.26226/zero c 1 5
			if (($? != 0)) ; then
				log_fail "Expected option: ${args[((i+1))]} \n" \
					"Can't create device, dev option failed"
			fi
		else
			$ECHO $msg | $GREP "${args[((i+1))]}" > /dev/null 2>&1
			if (($? != 0)) ; then
				log_fail "Expected option: ${args[((i+1))]} \n" \
					 "Real option: $msg"
			fi
		fi
	fi

	log_must $UMOUNT $tmpmnt
	((i += 2))
done

log_pass "With legacy mount, FSType-specific option works well passed."
