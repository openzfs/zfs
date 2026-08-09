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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify zpool status command mode (-c) works with ZPOOL_SCRIPTS_PATH
# defined.
#
# STRATEGY:
#	1. Set ZPOOL_SCRIPTS_PATH to contain a couple of non-default dirs
#	2. Make a simple script that echoes a key value pair in each dir
#	3. Make sure scripts can be run with -c
#	4. Remove the scripts we created

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

typeset SCRIPT_1="$TEST_BASE_DIR/scripts1/test1"
typeset SCRIPT_2="$TEST_BASE_DIR/scripts2/test2"

function cleanup
{
	log_must rm -rf $(dirname "$SCRIPT_1")
	log_must rm -rf $(dirname "$SCRIPT_2")
}

log_assert "zpool status -c can run scripts from custom search path"

if [ -e "$SCRIPT_1" ]; then
	log_fail "$SCRIPT_1 already exists."
fi

if [ -e "$SCRIPT_2" ]; then
	log_fail "$SCRIPT_2 already exists."
fi

log_onexit cleanup

# change zpool status search path
export ZPOOL_SCRIPTS_PATH="$(dirname $SCRIPT_1):$(dirname $SCRIPT_2)"

# create simple script in each dir
log_must mkdir -p $(dirname "$SCRIPT_1")
cat > "$SCRIPT_1" << EOF
#!/bin/sh
echo "USRCOL1=USRVAL1"
EOF
log_must chmod +x "$SCRIPT_1"

log_must mkdir -p $(dirname "$SCRIPT_2")
cat > "$SCRIPT_2" << EOF
#!/bin/sh
echo "USRCOL2=USRVAL2"
EOF
log_must chmod +x "$SCRIPT_2"

# test that we can run the scripts
typeset CMD_1=$(basename "$SCRIPT_1")
typeset CMD_2=$(basename "$SCRIPT_2")
test_zpool_script "$CMD_1" "$TESTPOOL" "zpool status -P -c"
test_zpool_script "$CMD_2" "$TESTPOOL" "zpool status -P -c"
test_zpool_script "$CMD_2,$CMD_1" "$TESTPOOL" "zpool status -P -c"

log_pass "zpool status -c can run scripts from custom search path passed"
