#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#

#
# Description:
#
# Test whether zhack label repair can recover
# corrupted checksums on devices of varied size,
# but not undetached devices.
#
# Strategy:
#
# 1. Create pool on a loopback device with some test data
# 2. Export the pool.
# 3. Corrupt all label checksums in the pool
# 4. Check that pool cannot be imported
# 5. Verify that it cannot be imported after using zhack label repair -u
#    to ensure that the -u option will quit on corrupted checksums.
# 6. Use zhack label repair -c on device
# 7. Check that pool can be imported and that data is intact

. "$STF_SUITE"/tests/functional/cli_root/zhack/library.kshlib

run_test_one "$(get_devsize)"
