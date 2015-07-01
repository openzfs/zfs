# ZFS Test Suite README

1) Building and installing the ZFS Test Suite

The ZFS Test Suite runs under the testrunner framework.  This framework
is built along side the standard ZFS utilities and is included as part of
zfs-test package.  The zfs-test package can be built from source as follows:

        ./configure --with-config=user
        make pkg-utils

The resulting packages can be installed using the rpm or dpkg command as
appropriate for your distributions.  Alternately, if you have installed
ZFS from a distributions repository (not from source) the zfs-test package
may be provided for your distribution.

        - Installed from source
        rpm -ivh ./zfs-test*.rpm, or
        yum install zfs-test

        - Installed from package repository
        dpkg -i ./zfs-test*.deb,
        apt-get install zfs-test

2) Running the ZFS Test Suite

The pre-requisites for running the ZFS Test Suite are:

  * Three scratch disks
    * Specify the disks you wish to use in the $DISKS variable, as a
      space delimited list like this: DISKS='vdb vdc vdd'
  * A non-root user with the full set of basic privileges and the ability
    to sudo(1M) to root without a password to run the test.
  * Specify any pools you wish to preserve as a space delimited list in
    the $KEEP variable. All pools detected at the start of testing are
    added automatically.
  * The ZFS Test Suite will add users and groups to test machine to
    verify functionality.  Therefore it is strongly advised that a
    dedicated test machine, which can be a VM, be used for testing.

Once the pre-requisites are satisfied, simply run the zfs-tests.sh script:

        test_machine$ /usr/share/zfs/zfs-tests.sh

Alternately, the zfs-tests.sh script can be run from the source tree to allow
developers to rapidly validate their work.  In this mode the ZFS utilities and
modules from the source tree will be used (rather than those installed on the
system).  In order to avoid certain types of failures you will need to ensure
to udev rules are installed.  This can be done manually or by ensuring some
version of ZFS is installed on the system.

        test_machine$ ./scripts/zfs-tests.sh

When the '-q' option is specified, it is passed to test-runner(1) which causes
output to be written to the console only for tests that do not pass and the
results summary.

When the '-v' option is specified, additional information describing the
test environment will be logged prior to invoking test-runner.  This includes
the runfile being used, the DISKS targeted, pools to keep, etc.

When the '-x' option is specified, the script will attempt to remove any
leftover configuration from a previous test run.  This includes destroying
any pools named testpool.*, unused DM devices, and loopback devices backed
by file-vdevs.  This operation can be DANGEROUS because it is possible that
the script will mistakenly remove a resource not related to the testing.

When the '-k' option is specified, the zfs-tests.sh script will not perform
any additional cleanup when test-runner exists.  This is useful when the
results of a specific test need to be preserved for further analysis.

The ZFS Test Suite allows the user to specify a subset of the tests via a
runfile. The format of the runfile is explained in test-runner(1), and
the files that zfs-tests.sh uses are available for reference under
/usr/share/zfs/runfiles. To specify a custom runfile, use the -r option:

        test_machine$ /usr/share/zfs/zfs-tests.sh -r my_tests.run

3) Test results

While the ZFS Test Suite is running, one informational line is printed at the
end of each test, and a results summary is printed at the end of the run. The
results summary includes the location of the complete logs, which is of the
form /var/tmp/test_results/[ISO 8601 date].
