# ZFS Test Suite README

1) Building and installing the ZFS Test Suite

The ZFS Test Suite runs under the test-runner framework.  This framework
is built along side the standard ZFS utilities and is included as part of
zfs-test package.  The zfs-test package can be built from source as follows:

    $ ./configure
    $ make pkg-utils

The resulting packages can be installed using the rpm or dpkg command as
appropriate for your distributions.  Alternately, if you have installed
ZFS from a distributions repository (not from source) the zfs-test package
may be provided for your distribution.

    - Installed from source
    $ rpm -ivh ./zfs-test*.rpm, or
    $ dpkg -i ./zfs-test*.deb,

    - Installed from package repository
    $ yum install zfs-test
    $ apt-get install zfs-test

2) Running the ZFS Test Suite

The pre-requisites for running the ZFS Test Suite are:

  * Three scratch disks
    * Specify the disks you wish to use in the $DISKS variable, as a
      space delimited list like this: DISKS='vdb vdc vdd'.  By default
      the zfs-tests.sh script will construct three loopback devices to
      be used for testing: DISKS='loop0 loop1 loop2'.
  * A non-root user with a full set of basic privileges and the ability
    to sudo(8) to root without a password to run the test.
  * Specify any pools you wish to preserve as a space delimited list in
    the $KEEP variable. All pools detected at the start of testing are
    added automatically.
  * The ZFS Test Suite will add users and groups to test machine to
    verify functionality.  Therefore it is strongly advised that a
    dedicated test machine, which can be a VM, be used for testing.

Once the pre-requisites are satisfied simply run the zfs-tests.sh script:

    $ /usr/share/zfs/zfs-tests.sh

Alternately, the zfs-tests.sh script can be run from the source tree to allow
developers to rapidly validate their work.  In this mode the ZFS utilities and
modules from the source tree will be used (rather than those installed on the
system).  In order to avoid certain types of failures you will need to ensure
the ZFS udev rules are installed.  This can be done manually or by ensuring
some version of ZFS is installed on the system.

    $ ./scripts/zfs-tests.sh

The following zfs-tests.sh options are supported:

    -v          Verbose zfs-tests.sh output When specified additional
                information describing the test environment will be logged
                prior to invoking test-runner.  This includes the runfile
                being used, the DISKS targeted, pools to keep, etc.

    -q          Quiet test-runner output.  When specified it is passed to
                test-runner(1) which causes output to be written to the
                console only for tests that do not pass and the results
                summary.

    -x          Remove all testpools, dm, lo, and files (unsafe).  When
                specified the script will attempt to remove any leftover
                configuration from a previous test run.  This includes
                destroying any pools named testpool, unused DM devices,
                and loopback devices backed by file-vdevs.  This operation
                can be DANGEROUS because it is possible that the script
                will mistakenly remove a resource not related to the testing.

    -k          Disable cleanup after test failure.  When specified the
                zfs-tests.sh script will not perform any additional cleanup
                when test-runner exists.  This is useful when the results of
                a specific test need to be preserved for further analysis.

    -f          Use sparse files directly instead of loopback devices for
                the testing.  When running in this mode certain tests will
                be skipped which depend on real block devices.

    -c          Only create and populate constrained path

    -I NUM      Number of iterations

    -d DIR      Create sparse files for vdevs in the DIR directory.  By
                default these files are created under /var/tmp/.

    -s SIZE     Use vdevs of SIZE (default: 4G)

    -r RUNFILES Run tests in RUNFILES (default: common.run,linux.run)

    -t PATH     Run single test at PATH relative to test suite

    -T TAGS     Comma separated list of tags (default: 'functional')

    -u USER     Run single test as USER (default: root)


The ZFS Test Suite allows the user to specify a subset of the tests via a
runfile or list of tags.

The format of the runfile is explained in test-runner(1), and
the files that zfs-tests.sh uses are available for reference under
/usr/share/zfs/runfiles. To specify a custom runfile, use the -r option:

    $ /usr/share/zfs/zfs-tests.sh -r my_tests.run

Otherwise user can set needed tags to run only specific tests.

3) Test results

While the ZFS Test Suite is running, one informational line is printed at the
end of each test, and a results summary is printed at the end of the run. The
results summary includes the location of the complete logs, which is logged in
the form `/var/tmp/test_results/[ISO 8601 date]`.  A normal test run launched
with the `zfs-tests.sh` wrapper script will look something like this:

    $ /usr/share/zfs/zfs-tests.sh -v -d /tmp/test

    --- Configuration ---
    Runfile:         /usr/share/zfs/runfiles/linux.run
    STF_TOOLS:       /usr/share/zfs/test-runner
    STF_SUITE:       /usr/share/zfs/zfs-tests
    STF_PATH:        /var/tmp/constrained_path.G0Sf
    FILEDIR:         /tmp/test
    FILES:           /tmp/test/file-vdev0 /tmp/test/file-vdev1 /tmp/test/file-vdev2
    LOOPBACKS:       /dev/loop0 /dev/loop1 /dev/loop2
    DISKS:           loop0 loop1 loop2
    NUM_DISKS:       3
    FILESIZE:        4G
    ITERATIONS:      1
    TAGS:            functional
    Keep pool(s):    rpool


    /usr/share/zfs/test-runner/bin/test-runner.py  -c /usr/share/zfs/runfiles/linux.run \
        -T functional -i /usr/share/zfs/zfs-tests -I 1
    Test: /usr/share/zfs/zfs-tests/tests/functional/arc/setup (run as root) [00:00] [PASS]
    ...more than 1100 additional tests...
    Test: /usr/share/zfs/zfs-tests/tests/functional/zvol/zvol_swap/cleanup (run as root) [00:00] [PASS]

    Results Summary
    SKIP	  52
    PASS	 1129
    
    Running Time:	02:35:33
    Percent passed:	95.6%
    Log directory:	/var/tmp/test_results/20180515T054509
