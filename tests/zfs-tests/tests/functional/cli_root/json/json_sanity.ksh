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
