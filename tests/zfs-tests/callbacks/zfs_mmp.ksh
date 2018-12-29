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
typeset history=$(</sys/module/zfs/parameters/zfs_multihost_history)

if [ $history -eq 0 ]; then
	exit
fi

for f in /proc/spl/kstat/zfs/*/multihost; do
	echo "================================================================="
	echo " Last $lines lines of $f"
	echo "================================================================="

	sudo tail -n $lines $f
done

echo "================================================================="
echo " End of zfs multihost log"
echo "================================================================="
