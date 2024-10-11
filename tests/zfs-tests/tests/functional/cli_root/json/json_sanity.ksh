#!/bin/ksh -p
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

# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Basic sanity check for valid JSON from zfs & zpool commands.
#
# STRATEGY:
# 1. Run different zfs/zpool -j commands and check for valid JSON

#
# -j and --json mean the same thing. Each command will be run twice, replacing
# JSONFLAG with the flag under test.
list=(
    "zpool status JSONFLAG -g --json-int --json-flat-vdevs --json-pool-key-guid"
    "zpool status -p JSONFLAG -g --json-int --json-flat-vdevs --json-pool-key-guid"
    "zpool status JSONFLAG -c upath"
    "zpool status JSONFLAG"
    "zpool status JSONFLAG testpool1"
    "zpool list JSONFLAG"
    "zpool list JSONFLAG -g"
    "zpool list JSONFLAG -o fragmentation"
    "zpool get JSONFLAG size"
    "zpool get JSONFLAG all"
    "zpool version JSONFLAG"
    "zfs list JSONFLAG"
    "zfs list JSONFLAG testpool1"
    "zfs get JSONFLAG all"
    "zfs get JSONFLAG available"
    "zfs mount JSONFLAG"
    "zfs version JSONFLAG"
)

function run_json_tests
{
	typeset flag=$1
	for cmd in "${list[@]}" ; do
	    cmd=${cmd//JSONFLAG/$flag}
	    log_must eval "$cmd | jq > /dev/null"
	done
}

log_must run_json_tests -j
log_must run_json_tests --json

log_pass "zpool and zfs commands outputted valid JSON"
