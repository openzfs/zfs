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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Try each 'zpool checkpoint' and relevant 'zpool import' with
#	invalid inputs to ensure it returns an error. That includes:
#		* A non-existent pool name or no pool name at all is supplied
#		* Pool supplied for discarding or rewinding but the pool
#		  does not have a checkpoint
#		* A dataset or a file/directory are supplied instead of a pool
#
# STRATEGY:
#	1. Create an array of parameters for the different scenarios
#	2. For each parameter, execute the scenarios sub-command
#	3. Verify that an error was returned
#

verify_runnable "global"

setup_test_pool
log_onexit cleanup_test_pool
populate_test_pool

#
# Argument groups below. Note that all_args also includes
# an empty string as "run command with no argument".
#
set -A all_args "" "-d" "--discard"

#
# Target groups below. Note that invalid_targets includes
# an empty string as "do not supply a pool name".
#
set -A invalid_targets "" "iDontExist" "$FS0" "$FS0FILE"
non_checkpointed="$TESTPOOL"

#
# Scenario 1
# Trying all checkpoint args with all invalid targets
#
typeset -i i=0
while (( i < ${#invalid_targets[*]} )); do
	typeset -i j=0
	while (( j < ${#all_args[*]} )); do
		log_mustnot zpool checkpoint ${all_args[j]} \
			${invalid_targets[i]}
		((j = j + 1))
	done
	((i = i + 1))
done

#
# Scenario 2
# If the pool does not have a checkpoint, -d nor import rewind
# should work with it.
#
log_mustnot zpool checkpoint -d $non_checkpointed
log_must zpool export $non_checkpointed
log_mustnot zpool import --rewind-to-checkpoint $non_checkpointed
log_must zpool import $non_checkpointed

log_pass "Badly formed checkpoint related commands with " \
	"invalid inputs fail as expected."
