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

export OS_TESTS="/opt/os-tests"
runner="/opt/test-runner/bin/run"

function fail
{
	echo $1
	exit ${2:-1}
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

	[[ -n $distro ]] && echo $OS_TESTS/runfiles/$distro.run
}

while getopts c: c; do
	case $c in
	'c')
		runfile=$OPTARG
		[[ -f $runfile ]] || fail "Cannot read file: $runfile"
		;;
	esac
done
shift $((OPTIND - 1))

[[ -z $runfile ]] && runfile=$(find_runfile)
[[ -z $runfile ]] && fail "Couldn't determine distro"

$runner -c $runfile

exit $?
