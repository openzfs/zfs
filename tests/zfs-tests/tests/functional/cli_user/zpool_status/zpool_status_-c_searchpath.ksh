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
