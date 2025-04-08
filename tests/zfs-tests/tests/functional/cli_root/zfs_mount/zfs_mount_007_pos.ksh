#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
# The following options can be set on a temporary basis using the -o option
# without affecting the on-disk property. The original on-disk value will be
# restored when the file system is unmounted and mounted.
#
#         PROPERTY		MOUNT OPTION
#	  atime			atime/noatime
#	  devices		devices/nodevices
#	  exec			exec/noexec
#	  readonly		ro/rw
#	  setuid		setuid/nosetuid
#
# STRATEGY:
#	1. Create filesystem and get original property value.
#	2. Using 'zfs mount -o' to set filesystem property.
#	3. Verify the property was set temporarily.
#	4. Verify it will not affect the property that is stored on disk.
#

function cleanup
{
	if ! ismounted $TESTPOOL/$TESTFS; then
		log_must zfs mount $TESTPOOL/$TESTFS
	fi
}

log_assert "Verify '-o' will set filesystem property temporarily, " \
	"without affecting the property that is stored on disk."
log_onexit cleanup

set -A properties "atime" "exec" "readonly" "setuid"
if ! is_freebsd; then
	properties+=("devices")
fi

#
# Get the specified filesystem property reverse mount option.
#
# $1 filesystem
# $2 property
#
function get_reverse_option
{
	typeset fs=$1
	typeset prop=$2

	# Define property value: "reverse if value=on" "reverse if value=off"
	if is_linux; then
		set -A values "noatime"   "atime" \
			      "noexec"    "exec" \
			      "rw"        "ro" \
			      "nosuid"    "suid" \
			      "nodev"     "dev"
	elif is_freebsd; then
		set -A values "noatime"   "atime" \
			      "noexec"    "exec" \
			      "rw"        "ro" \
			      "nosetuid"  "setuid"
	else
		set -A values "noatime"   "atime" \
			      "noexec"    "exec" \
			      "rw"        "ro" \
			      "nosetuid"  "setuid" \
			      "nodevices" "devices"
	fi

	typeset -i i=0
	while (( i < ${#properties[@]} )); do
		if [[ $prop == ${properties[$i]} ]]; then
			break
		fi

		(( i += 1 ))
	done
	if (( i >= ${#properties[@]} )); then
		log_fail "Incorrect option: $prop"
	fi

	typeset val
	typeset -i ind=0
	val=$(get_prop $prop $fs)
	if [[ $val == "on" ]]; then
		(( ind = i * 2 ))
	else
		(( ind = i * 2 + 1 ))
	fi

	echo ${values[$ind]}
}

fs=$TESTPOOL/$TESTFS
cleanup

for property in ${properties[@]}; do
	orig_val=$(get_prop $property $fs)

	# Set filesystem property temporarily
	reverse_opt=$(get_reverse_option $fs $property)
	log_must zfs unmount $fs
	log_must zfs mount -o $reverse_opt $fs

	cur_val=$(get_prop $property $fs)

	# In LZ, a user with all zone privileges can never with "devices"
	if ! is_global_zone && [[ $property == devices ]] ; then
		if [[ $cur_val != off || $orig_val != off ]]; then
			log_fail "'devices' property shouldn't " \
				"be enabled in LZ"
		fi
	elif [[ $orig_val == $cur_val ]]; then
		log_fail "zfs mount -o $reverse_opt " \
			"doesn't change property."
	fi

	# unmount & mount will revert property to the original value
	log_must zfs unmount $fs
	log_must zfs mount $fs

	cur_val=$(get_prop $property $fs)
	if [[ $orig_val != $cur_val ]]; then
		log_fail "zfs mount -o $reverse_opt " \
			"change the property that is stored on disks"
	fi
done

log_pass "Verify '-o' set filesystem property temporarily passed."
