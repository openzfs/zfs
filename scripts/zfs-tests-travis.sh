#!/bin/bash
#
# This script splits linux.run file for zfs-tests to run only part 
# of it in each travis-ci job.
#
# Copyright (c) 2017 George Melikov. All rights reserved.
#
# env variables example:
#ZFS_TEST_TRAVIS_BUILDER_FROM='[tests/functional/acl/posix]'
#ZFS_TEST_TRAVIS_BUILDER_TO='[tests/functional/poolversion]'
#
#ZFS_TEST_TRAVIS_BUILDER_FROM - first test to run
#ZFS_TEST_TRAVIS_BUILDER_TO - previous test will be run, and this test 
# will be used to remove all after it's line (included). If not defined -
# don't remove any.
#

RUNFILE="/usr/share/zfs/runfiles/linux.run"
ZFS_TEST_TRAVIS_PATH="/usr/share/zfs/zfs-tests.sh"

#
# Output a useful usage message.
#
usage() {
cat << EOF
USAGE:
$0 [-f FROM] [-t TO] [-z ZFSTESTS_PATH]

DESCRIPTION:
	Split tests and launch ZFS Test Suite

OPTIONS:
	-h          Show this message
	-f          First test
	-t          Last test (will be excluded)
	-z          Path to ZFS Test Suite

EXAMPLES:
# Run the default (linux) suite of tests and output the configuration used.
$0 -f '[tests/functional/acl/posix]' -t '[tests/functional/poolversion]'
-z /usr/share/zfs/zfs-tests.sh
EOF
}

#
# Main function
#
main() {
# prepare temporary files
ZFS_TEST_COUNT_DEFAULTEND_LINE=$(awk -vn1=2 '/^\[/{++n; if (n==n1) { print NR; exit}}' ./tests/runfiles/linux.run)
awk "NR < $ZFS_TEST_COUNT_DEFAULTEND_LINE" $RUNFILE > /tmp/linux.run.header
awk "NR >= $ZFS_TEST_COUNT_DEFAULTEND_LINE" $RUNFILE > /tmp/linux.run.tmp

#find lines numbers
ZFS_TEST_COUNT_TESTS_FIRSTTEST_LINE=$(grep -nwF $ZFS_TEST_TRAVIS_BUILDER_FROM /tmp/linux.run.tmp | cut -f1 -d:)
# remove all before needed tests
awk "NR >= $ZFS_TEST_COUNT_TESTS_FIRSTTEST_LINE" /tmp/linux.run.tmp > /tmp/linux.run.tmp_ && mv /tmp/linux.run.tmp{_,}

# if last test is defined - delete all after it
if [ -n "$ZFS_TEST_TRAVIS_BUILDER_TO" ] ; then 
	ZFS_TEST_COUNT_TESTS_LASTTEST_LINE=$(grep -nwF $ZFS_TEST_TRAVIS_BUILDER_TO /tmp/linux.run.tmp | cut -f1 -d:)
	awk "NR < $ZFS_TEST_COUNT_TESTS_LASTTEST_LINE" /tmp/linux.run.tmp > /tmp/linux.run.tmp_ && mv /tmp/linux.run.tmp{_,}
fi

# merge tests with [DEFAULT] section
cat /tmp/linux.run.header /tmp/linux.run.tmp > /tmp/linux.run

# run zfs-tests
$ZFS_TEST_TRAVIS_PATH -v -r /tmp/linux.run
}



while getopts 'hvqxkfd:s:r:?' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	f)
		ZFS_TEST_TRAVIS_BUILDER_FROM="$OPTARG"
		;;
	t)
		ZFS_TEST_TRAVIS_BUILDER_TO="$OPTARG"
		;;
	z)
		ZFS_TEST_TRAVIS_PATH="$OPTARG"
		;;
	?)
		usage
		exit
		;;
	esac
done

main
