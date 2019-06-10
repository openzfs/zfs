#!/usr/bin/env python3

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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#
# This script must remain compatible with Python 2.6+ and Python 3.4+.
#

import os
import re
import sys

#
# This script parses the stdout of zfstest, which has this format:
#
# Test: /path/to/testa (run as root) [00:00] [PASS]
# Test: /path/to/testb (run as jkennedy) [00:00] [PASS]
# Test: /path/to/testc (run as root) [00:00] [FAIL]
# [...many more results...]
#
# Results Summary
# FAIL      22
# SKIP      32
# PASS    1156
#
# Running Time:   02:50:31
# Percent passed: 95.5%
# Log directory:  /var/tmp/test_results/20180615T205926
#

#
# Common generic reasons for a test or test group to be skipped.
#
# Some test cases are known to fail in ways which are not harmful or dangerous.
# In these cases simply mark the test as a known failure until it can be
# updated and the issue resolved.  Note that it's preferable to open a unique
# issue on the GitHub issue tracker for each test case failure.
#
known_reason = 'Known issue'

#
# Some tests require that a test user be able to execute the zfs utilities.
# This may not be possible when testing in-tree due to the default permissions
# on the user's home directory.  When testing this can be resolved by granting
# group read access.
#
# chmod 0750 $HOME
#
exec_reason = 'Test user execute permissions required for utilities'

#
# Some tests require that the DISKS provided can be partitioned.  This is
# normally not an issue because loop back devices are used for DISKS and they
# can be partition.  There is one notable exception, the CentOS 6.x kernel is
# old enough that it does not support partitioning loop back devices.
#
disk_reason = 'Partitionable DISKS required'

#
# Some tests require a minimum python version of 3.5 and will be skipped when
# the default system version is too old.  There may also be tests which require
# additional python modules be installed, for example python-cffi is required
# by the pyzfs tests.
#
python_reason = 'Python v3.5 or newer required'
python_deps_reason = 'Python modules missing: python-cffi'

#
# Some tests require the O_TMPFILE flag which was first introduced in the
# 3.11 kernel.
#
tmpfile_reason = 'Kernel O_TMPFILE support required'

#
# Some tests may depend on udev change events being generated when block
# devices change capacity.  This functionality wasn't available until the
# 2.6.38 kernel.
#
udev_reason = 'Kernel block device udev change events required'

#
# Some tests require that the NFS client and server utilities be installed.
#
share_reason = 'NFS client and server utilities required'

#
# Some tests require that the lsattr utility support the project id feature.
#
project_id_reason = 'lsattr with set/show project ID required'

#
# Some tests require that the kernel support user namespaces.
#
user_ns_reason = 'Kernel user namespace support required'

#
# Some rewind tests can fail since nothing guarantees that old MOS blocks
# are not overwritten.  Snapshots protect datasets and data files but not
# the MOS.  Reasonable efforts are made in the test case to increase the
# odds that some txgs will have their MOS data left untouched, but it is
# never a sure thing.
#
rewind_reason = 'Arbitrary pool rewind is not guaranteed'

#
# Some tests may by structured in a way that relies on exact knowledge
# of how much free space in available in a pool.  These tests cannot be
# made completely reliable because the internal details of how free space
# is managed are not exposed to user space.
#
enospc_reason = 'Exact free space reporting is not guaranteed'

#
# Some tests require a minimum version of the fio benchmark utility.
# Older distributions such as CentOS 6.x only provide fio-2.0.13.
#
fio_reason = 'Fio v2.3 or newer required'

#
# Some tests require that the DISKS provided support the discard operation.
# Normally this is not an issue because loop back devices are used for DISKS
# and they support discard (TRIM/UNMAP).
#
trim_reason = 'DISKS must support discard (TRIM/UNMAP)'

#
# Some tests are not applicable to Linux or need to be updated to operate
# in the manor required by Linux.  Any tests which are skipped for this
# reason will be suppressed in the final analysis output.
#
na_reason = "N/A on Linux"

summary = {
    'total': float(0),
    'passed': float(0),
    'logfile': "Could not determine logfile location."
}

#
# These tests are known to fail, thus we use this list to prevent these
# failures from failing the job as a whole; only unexpected failures
# bubble up to cause this script to exit with a non-zero exit status.
#
# Format: { 'test-name': ['expected result', 'issue-number | reason'] }
#
# For each known failure it is recommended to link to a GitHub issue by
# setting the reason to the issue number.  Alternately, one of the generic
# reasons listed above can be used.
#
known = {
    'casenorm/sensitive_formd_lookup': ['FAIL', '7633'],
    'casenorm/sensitive_formd_delete': ['FAIL', '7633'],
    'casenorm/mixed_none_lookup_ci': ['FAIL', '7633'],
    'casenorm/mixed_formd_lookup': ['FAIL', '7633'],
    'casenorm/mixed_formd_lookup_ci': ['FAIL', '7633'],
    'casenorm/mixed_formd_delete': ['FAIL', '7633'],
    'cli_root/zfs_receive/zfs_receive_004_neg': ['FAIL', known_reason],
    'cli_root/zfs_unshare/zfs_unshare_002_pos': ['SKIP', na_reason],
    'cli_root/zfs_unshare/zfs_unshare_006_pos': ['SKIP', na_reason],
    'cli_root/zpool_create/zpool_create_016_pos': ['SKIP', na_reason],
    'cli_user/misc/zfs_share_001_neg': ['SKIP', na_reason],
    'cli_user/misc/zfs_unshare_001_neg': ['SKIP', na_reason],
    'inuse/inuse_001_pos': ['SKIP', na_reason],
    'inuse/inuse_003_pos': ['SKIP', na_reason],
    'inuse/inuse_006_pos': ['SKIP', na_reason],
    'inuse/inuse_007_pos': ['SKIP', na_reason],
    'privilege/setup': ['SKIP', na_reason],
    'refreserv/refreserv_004_pos': ['FAIL', known_reason],
    'removal/removal_condense_export': ['SKIP', known_reason],
    'removal/removal_with_zdb': ['SKIP', known_reason],
    'rootpool/setup': ['SKIP', na_reason],
    'rsend/rsend_008_pos': ['SKIP', '6066'],
    'snapshot/rollback_003_pos': ['SKIP', '6143'],
    'vdev_zaps/vdev_zaps_007_pos': ['FAIL', known_reason],
    'xattr/xattr_008_pos': ['SKIP', na_reason],
    'xattr/xattr_009_neg': ['SKIP', na_reason],
    'xattr/xattr_010_neg': ['SKIP', na_reason],
    'zvol/zvol_misc/zvol_misc_001_neg': ['SKIP', na_reason],
    'zvol/zvol_misc/zvol_misc_003_neg': ['SKIP', na_reason],
    'zvol/zvol_misc/zvol_misc_004_pos': ['SKIP', na_reason],
    'zvol/zvol_misc/zvol_misc_005_neg': ['SKIP', na_reason],
    'zvol/zvol_misc/zvol_misc_006_pos': ['SKIP', na_reason],
    'zvol/zvol_swap/zvol_swap_003_pos': ['SKIP', na_reason],
    'zvol/zvol_swap/zvol_swap_005_pos': ['SKIP', na_reason],
    'zvol/zvol_swap/zvol_swap_006_pos': ['SKIP', na_reason],
}

#
# These tests may occasionally fail or be skipped.  We want there failures
# to be reported but only unexpected failures should bubble up to cause
# this script to exit with a non-zero exit status.
#
# Format: { 'test-name': ['expected result', 'issue-number | reason'] }
#
# For each known failure it is recommended to link to a GitHub issue by
# setting the reason to the issue number.  Alternately, one of the generic
# reasons listed above can be used.
#
maybe = {
    'cache/setup': ['SKIP', disk_reason],
    'cache/cache_010_neg': ['FAIL', known_reason],
    'chattr/setup': ['SKIP', exec_reason],
    'cli_root/zdb/zdb_006_pos': ['FAIL', known_reason],
    'cli_root/zfs_get/zfs_get_004_pos': ['FAIL', known_reason],
    'cli_root/zfs_get/zfs_get_009_pos': ['SKIP', '5479'],
    'cli_root/zfs_rollback/zfs_rollback_001_pos': ['FAIL', '6415'],
    'cli_root/zfs_rollback/zfs_rollback_002_pos': ['FAIL', '6416'],
    'cli_root/zfs_share/setup': ['SKIP', share_reason],
    'cli_root/zfs_snapshot/zfs_snapshot_002_neg': ['FAIL', known_reason],
    'cli_root/zfs_unshare/setup': ['SKIP', share_reason],
    'cli_root/zpool_add/setup': ['SKIP', disk_reason],
    'cli_root/zpool_add/zpool_add_004_pos': ['FAIL', known_reason],
    'cli_root/zpool_create/setup': ['SKIP', disk_reason],
    'cli_root/zpool_create/zpool_create_008_pos': ['FAIL', known_reason],
    'cli_root/zpool_destroy/zpool_destroy_001_pos': ['SKIP', '6145'],
    'cli_root/zpool_expand/setup': ['SKIP', udev_reason],
    'cli_root/zpool_export/setup': ['SKIP', disk_reason],
    'cli_root/zpool_import/setup': ['SKIP', disk_reason],
    'cli_root/zpool_import/import_rewind_device_replaced':
        ['FAIL', rewind_reason],
    'cli_root/zpool_import/import_rewind_config_changed':
        ['FAIL', rewind_reason],
    'cli_root/zpool_import/zpool_import_missing_003_pos': ['SKIP', '6839'],
    'cli_root/zpool_remove/setup': ['SKIP', disk_reason],
    'cli_root/zpool_trim/setup': ['SKIP', trim_reason],
    'cli_root/zpool_upgrade/zpool_upgrade_004_pos': ['FAIL', '6141'],
    'cli_user/misc/arc_summary3_001_pos': ['SKIP', python_reason],
    'delegate/setup': ['SKIP', exec_reason],
    'fault/auto_online_001_pos': ['SKIP', disk_reason],
    'fault/auto_replace_001_pos': ['SKIP', disk_reason],
    'history/history_004_pos': ['FAIL', '7026'],
    'history/history_005_neg': ['FAIL', '6680'],
    'history/history_006_neg': ['FAIL', '5657'],
    'history/history_008_pos': ['FAIL', known_reason],
    'history/history_010_pos': ['SKIP', exec_reason],
    'inuse/inuse_005_pos': ['SKIP', disk_reason],
    'inuse/inuse_008_pos': ['SKIP', disk_reason],
    'inuse/inuse_009_pos': ['SKIP', disk_reason],
    'io/mmap': ['SKIP', fio_reason],
    'largest_pool/largest_pool_001_pos': ['FAIL', known_reason],
    'pyzfs/pyzfs_unittest': ['SKIP', python_deps_reason],
    'no_space/enospc_002_pos': ['FAIL', enospc_reason],
    'projectquota/setup': ['SKIP', exec_reason],
    'redundancy/redundancy_004_neg': ['FAIL', '7290'],
    'reservation/reservation_008_pos': ['FAIL', '7741'],
    'reservation/reservation_018_pos': ['FAIL', '5642'],
    'rsend/rsend_019_pos': ['FAIL', '6086'],
    'rsend/rsend_020_pos': ['FAIL', '6446'],
    'rsend/rsend_021_pos': ['FAIL', '6446'],
    'rsend/rsend_024_pos': ['FAIL', '5665'],
    'rsend/send-c_volume': ['FAIL', '6087'],
    'snapshot/clone_001_pos': ['FAIL', known_reason],
    'snapshot/snapshot_009_pos': ['FAIL', '7961'],
    'snapshot/snapshot_010_pos': ['FAIL', '7961'],
    'snapused/snapused_004_pos': ['FAIL', '5513'],
    'tmpfile/setup': ['SKIP', tmpfile_reason],
    'threadsappend/threadsappend_001_pos': ['FAIL', '6136'],
    'trim/setup': ['SKIP', trim_reason],
    'upgrade/upgrade_projectquota_001_pos': ['SKIP', project_id_reason],
    'user_namespace/setup': ['SKIP', user_ns_reason],
    'userquota/setup': ['SKIP', exec_reason],
    'vdev_zaps/vdev_zaps_004_pos': ['FAIL', '6935'],
    'write_dirs/setup': ['SKIP', disk_reason],
    'zvol/zvol_ENOSPC/zvol_ENOSPC_001_pos': ['FAIL', '5848'],
}


def usage(s):
    print(s)
    sys.exit(1)


def process_results(pathname):
    try:
        f = open(pathname)
    except IOError as e:
        print('Error opening file: %s' % e)
        sys.exit(1)

    prefix = '/zfs-tests/tests/functional/'
    pattern = \
        r'^Test:\s*\S*%s(\S+)\s*\(run as (\S+)\)\s*\[(\S+)\]\s*\[(\S+)\]' \
        % prefix
    pattern_log = r'^\s*Log directory:\s*(\S*)'

    d = {}
    for l in f.readlines():
        m = re.match(pattern, l)
        if m and len(m.groups()) == 4:
            summary['total'] += 1
            if m.group(4) == "PASS":
                summary['passed'] += 1
            d[m.group(1)] = m.group(4)
            continue

        m = re.match(pattern_log, l)
        if m:
            summary['logfile'] = m.group(1)

    return d


if __name__ == "__main__":
    if len(sys.argv) != 2:
        usage('usage: %s <pathname>' % sys.argv[0])
    results = process_results(sys.argv[1])

    if summary['total'] == 0:
        print("\n\nNo test results were found.")
        print("Log directory:  %s" % summary['logfile'])
        sys.exit(0)

    expected = []
    unexpected = []

    for test in list(results.keys()):
        if results[test] == "PASS":
            continue

        setup = test.replace(os.path.basename(test), "setup")
        if results[test] == "SKIP" and test != setup:
            if setup in known and known[setup][0] == "SKIP":
                continue
            if setup in maybe and maybe[setup][0] == "SKIP":
                continue

        if ((test not in known or results[test] not in known[test][0]) and
                (test not in maybe or results[test] not in maybe[test][0])):
            unexpected.append(test)
        else:
            expected.append(test)

    print("\nTests with results other than PASS that are expected:")
    for test in sorted(expected):
        issue_url = 'https://github.com/zfsonlinux/zfs/issues/'

        # Include the reason why the result is expected, given the following:
        # 1. Suppress test results which set the "N/A on Linux" reason.
        # 2. Numerical reasons are assumed to be GitHub issue numbers.
        # 3. When an entire test group is skipped only report the setup reason.
        if test in known:
            if known[test][1] == na_reason:
                continue
            elif known[test][1].isdigit():
                expect = issue_url + known[test][1]
            else:
                expect = known[test][1]
        elif test in maybe:
            if maybe[test][1].isdigit():
                expect = issue_url + maybe[test][1]
            else:
                expect = maybe[test][1]
        elif setup in known and known[setup][0] == "SKIP" and setup != test:
            continue
        elif setup in maybe and maybe[setup][0] == "SKIP" and setup != test:
            continue
        else:
            expect = "UNKNOWN REASON"
        print("    %s %s (%s)" % (results[test], test, expect))

    print("\nTests with result of PASS that are unexpected:")
    for test in sorted(known.keys()):
        # We probably should not be silently ignoring the case
        # where "test" is not in "results".
        if test not in results or results[test] != "PASS":
            continue
        print("    %s %s (expected %s)" % (results[test], test,
                                           known[test][0]))

    print("\nTests with results other than PASS that are unexpected:")
    for test in sorted(unexpected):
        expect = "PASS" if test not in known else known[test][0]
        print("    %s %s (expected %s)" % (results[test], test, expect))

    if len(unexpected) == 0:
        sys.exit(0)
    else:
        sys.exit(1)
