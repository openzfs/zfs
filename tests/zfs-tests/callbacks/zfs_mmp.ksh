#!/bin/ksh -p

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
# Copyright (c) 2017 by Lawrence Livermore National Security.
# All rights reserved.
#

# $1: number of lines to output (default: 40)
typeset lines=${1:-40}
typeset uname=$(uname)
if [ "$uname" = "FreeBSD" ]; then
	typeset history=$(sysctl -n vfs.zfs.multihost.history)
else
	typeset history=$(</sys/module/zfs/parameters/zfs_multihost_history)
fi
if [ $history -eq 0 ]; then
	exit
fi

if [ "$uname" = "FreeBSD" ]; then
	typeset kstats=$(sysctl -N kstat.zfs | grep "kstat.zfs.*.multihost")
else
	typeset kstats=$(echo /proc/spl/kstat/zfs/*/multihost)
fi
for kstat in $kstats; do
	echo "================================================================="
	echo " Last $lines lines of $kstat"
	echo "================================================================="

	if [ "$uname" = "FreeBSD" ]; then
		sysctl -n $kstat | tail -n $lines
	else
		sudo tail -n $lines $kstat
	fi
done

echo "================================================================="
echo " End of zfs multihost log"
echo "================================================================="
