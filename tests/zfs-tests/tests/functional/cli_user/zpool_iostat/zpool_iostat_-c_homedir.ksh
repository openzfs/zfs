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
#	Verify zpool iostat command mode (-c) works with scripts in user's
#	home directory.
#
# STRATEGY:
#	1. Change HOME to /var/tmp (TEST_BASE_DIR)
#	2. Make a simple script that echoes a key value pair
#	   in $HOME/.zpool.d
#	3. Make sure it can be run with -c
#	4. Remove the script we created

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

# In tree testing sets this variable, we need to unset it
# to restore zpool's search path.
unset ZPOOL_SCRIPTS_PATH

# change HOME
export HOME="$TEST_BASE_DIR"
typeset USER_SCRIPT_FULL="$HOME/.zpool.d/userscript"

function cleanup
{
	log_must rm -rf "$HOME/.zpool.d"
}

log_assert "zpool iostat -c can run scripts from ~/.zpool.d"

if [ -e "$USER_SCRIPT_FULL" ]; then
	log_fail "$USER_SCRIPT_FULL already exists."
fi

log_onexit cleanup

# create simple script
log_must mkdir -p "$HOME/.zpool.d"
cat > "$USER_SCRIPT_FULL" << EOF
#!/bin/sh
echo "USRCOL=USRVAL"
EOF
log_must chmod +x "$USER_SCRIPT_FULL"

# test that we can run the script
typeset USER_SCRIPT=$(basename "$USER_SCRIPT_FULL")
test_zpool_script "$USER_SCRIPT" "$TESTPOOL" "zpool iostat -P -c"

log_pass "zpool iostat -c can run scripts from ~/.zpool.d passed"
