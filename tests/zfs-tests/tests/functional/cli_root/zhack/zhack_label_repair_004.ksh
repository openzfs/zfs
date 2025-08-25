#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#

#
# Description:
#
# Test whether zhack label repair can recover a device of varied size with
# corrupted checksums and which has been detached (in one command).
#
# Strategy:
#
# 1. Create pool on a loopback device with some test data
# 2. Detach either device from the mirror
# 3. Export the pool
# 4. Remove the non-detached device and its backing file
# 5. Corrupt all label checksums on the remaining device
# 6. Verify that the remaining detached device cannot be imported
# 7. Use zhack label repair -cu on device to attempt to fix checksums and
#    undetach the device in a single operation.
# 8. Verify that the detached device can be imported and that data is intact

. "$STF_SUITE"/tests/functional/cli_root/zhack/library.kshlib

run_test_four "$(get_devsize)"
