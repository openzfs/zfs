#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/licenses/CDDL-1.0
#

#
# Copyright 2019 Richard Elling
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Test zdb options parsing. The options are complicated and testing
# with a live zdb executable is difficult. By setting the environment
# variable ZFS_OPTIONS_DEBUG, zdb will print the internally parsed
# options to stderr and exit without performing any operations. This
# allows testing of options parsing in a more complete, automated manner.
#

function compare_value  # key expected option ...
{
	typeset local key=$1
	shift
	typeset local expected=$1
	shift
	log_assert "key=$key count=$expected command=zdb $*"
	result=$(zdb $* 2>&1 1>/dev/null |
	    awk -v key=$key '$1 == key {print $NF;exit}')
	if [[ $result != $expected ]]; then
		log_fail "zdb parse result \"$result\", expected \"$expected\""
	fi
}
typeset TEST_DESC="zdb options parser"
log_assert $TEST_DESC

export ZFS_OPTIONS_DEBUG=true

compare_value l 1 -l
compare_value l 2 -ll
compare_value l 2 -l -l
compare_value l 2 --label --label
compare_value l 2 -l --label
compare_value l 6 -l -ll --label -l -l

compare_value d 1 -l -d
compare_value d 3 --dataset -dd

compare_value argc 0 -e --path /dir --path=/dir -p /dir
compare_value argc 1 -e --path /dir --path=/dir -p /dir poolname
compare_value argc 2 -d poolname object

# dump all sets many keys to value=2
compare_value argc 1 poolname
compare_value b 2 poolname
compare_value c 2 poolname
compare_value C 2 poolname
compare_value d 2 poolname
compare_value e 0 poolname
compare_value e 2 --exported poolname
compare_value E 0 poolname
compare_value G 2 poolname
compare_value h 2 poolname
compare_value i 2 poolname
compare_value I 2 poolname
compare_value k 0 poolname
compare_value l 0 poolname
compare_value L 0 poolname
compare_value m 2 poolname
compare_value M 2 poolname
compare_value o 2 poolname
compare_value O 0 poolname
compare_value p 2 poolname
compare_value P 0 poolname
compare_value q 2 poolname
compare_value R 0 poolname
compare_value s 2 poolname
compare_value S 0 poolname
compare_value t 2 poolname
compare_value u 2 poolname
compare_value U 2 poolname
compare_value v 2 poolname
compare_value V 2 poolname
compare_value x 2 poolname
compare_value X 0 poolname
compare_value Y 2 poolname
compare_value 256 0 poolname

# setting some options disables dump all
compare_value b 1 -b
compare_value c 0 -b
compare_value d 0 -b

# option 256 is --help, but it exits immediately after printing message
log_assert "zdb --help shows usage message"
RES=$(zdb --help 2>&1 1>/dev/null | awk '{print $1;exit}')
[[ $RES != "Usage:" ]] && log_fail zdb --help usage not found

log_pass $TEST_DESC
