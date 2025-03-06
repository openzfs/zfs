#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
