#!/usr/bin/ksh

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
# Copyright (c) 2012 by Delphix. All rights reserved.
# Copyright 2014, OmniTI Computer Consulting, Inc. All rights reserved.
#

export STF_SUITE="/opt/zfs-tests"
export STF_TOOLS="/opt/test-runner/stf"
runner="/opt/test-runner/bin/run"
auto_detect=false

function fail
{
	echo $1
	exit ${2:-1}
}

function find_disks
{
	typeset all_disks=$(echo '' | sudo /usr/sbin/format | awk \
	    '/c[0-9]/ {print $2}')
	typeset used_disks=$(/sbin/zpool status | awk \
	    '/c[0-9]*t[0-9a-f]*d[0-9]/ {print $1}' | sed 's/s[0-9]//g')

	typeset disk used avail_disks
	for disk in $all_disks; do
		for used in $used_disks; do
			[[ "$disk" = "$used" ]] && continue 2
		done
		[[ -n $avail_disks ]] && avail_disks="$avail_disks $disk"
		[[ -z $avail_disks ]] && avail_disks="$disk"
	done

	echo $avail_disks
}

function find_rpool
{
	typeset ds=$(/usr/sbin/mount | awk '/^\/ / {print $3}')
	echo ${ds%%/*}
}

function find_runfile
{
	typeset distro=
	if [[ -d /opt/delphix && -h /etc/delphix/version ]]; then
		distro=delphix
	elif [[ 0 -ne $(grep -c OpenIndiana /etc/release 2>/dev/null) ]]; then
		distro=openindiana
	elif [[ 0 -ne $(grep -c OmniOS /etc/release 2>/dev/null) ]]; then
		distro=omnios
	fi

	[[ -n $distro ]] && echo $STF_SUITE/runfiles/$distro.run
}

function verify_id
{
	[[ $(id -u) = "0" ]] && fail "This script must not be run as root."

	sudo -n id >/dev/null 2>&1
	[[ $? -eq 0 ]] || fail "User must be able to sudo without a password."

	typeset -i priv_cnt=$(ppriv $$ | egrep -v \
	    ": basic$|	L:| <none>|$$:" | wc -l)
	[[ $priv_cnt -ne 0 ]] && fail "User must only have basic privileges."
}

function verify_disks
{
	typeset disk
	for disk in $DISKS; do
		sudo /usr/sbin/prtvtoc /dev/rdsk/${disk}s0 >/dev/null 2>&1
		[[ $? -eq 0 ]] || return 1
	done
	return 0
}

verify_id

while getopts ac:q c; do
	case $c in
	'a')
		auto_detect=true
		;;
	'c')
		runfile=$OPTARG
		[[ -f $runfile ]] || fail "Cannot read file: $runfile"
		;;
	'q')
		quiet='-q'
		;;
	esac
done
shift $((OPTIND - 1))

# If the user specified -a, then use free disks, otherwise use those in $DISKS.
if $auto_detect; then
	export DISKS=$(find_disks)
elif [[ -z $DISKS ]]; then
	fail "\$DISKS not set in env, and -a not specified."
else
	verify_disks || fail "Couldn't verify all the disks in \$DISKS"
fi

# Add the rpool to $KEEP according to its contents. It's ok to list it twice.
if [[ -z $KEEP ]]; then
	export KEEP="^$(find_rpool)\$"
else
	export KEEP="^$(echo $KEEP | sed 's/ /|$/')\$"
	KEEP+="|^$(find_rpool)\$"
fi

[[ -z $runfile ]] && runfile=$(find_runfile)
[[ -z $runfile ]] && fail "Couldn't determine distro"

. $STF_SUITE/include/default.cfg

num_disks=$(echo $DISKS | awk '{print NF}')
[[ $num_disks -lt 3 ]] && fail "Not enough disks to run ZFS Test Suite"

$runner $quiet -c $runfile

exit $?
