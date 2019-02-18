#!/usr/bin/python

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
# Copyright (c) 2012, 2015 by Delphix. All rights reserved.
# Copyright (c) 2017 Datto Inc.
#

# some python 2.7 system don't have a configparser shim
try:
    import configparser
except ImportError:
    import ConfigParser as configparser

import os
import logging
from datetime import datetime
from optparse import OptionParser
from pwd import getpwnam
from pwd import getpwuid
from select import select
from subprocess import PIPE
from subprocess import Popen
from sys import argv
from sys import maxsize
from threading import Timer
from time import time

BASEDIR = '/var/tmp/test_results'
TESTDIR = '/usr/share/zfs/'
KILL = 'kill'
TRUE = 'true'
SUDO = 'sudo'


class Result(object):
    total = 0
    runresults = {'PASS': 0, 'FAIL': 0, 'SKIP': 0, 'KILLED': 0}

    def __init__(self):
        self.starttime = None
        self.returncode = None
        self.runtime = ''
        self.stdout = []
        self.stderr = []
        self.result = ''

    def done(self, proc, killed):
        """
        Finalize the results of this Cmd.
        """
        Result.total += 1
        m, s = divmod(time() - self.starttime, 60)
        self.runtime = '%02d:%02d' % (m, s)
        self.returncode = proc.returncode
        if killed:
            self.result = 'KILLED'
            Result.runresults['KILLED'] += 1
        elif self.returncode is 0:
            self.result = 'PASS'
            Result.runresults['PASS'] += 1
        elif self.returncode is 4:
            self.result = 'SKIP'
            Result.runresults['SKIP'] += 1
        elif self.returncode is not 0:
            self.result = 'FAIL'
            Result.runresults['FAIL'] += 1


class Output(object):
    """
    This class is a slightly modified version of the 'Stream' class found
    here: http://goo.gl/aSGfv
    """
    def __init__(self, stream):
        self.stream = stream
        self._buf = ''
        self.lines = []

    def fileno(self):
        return self.stream.fileno()

    def read(self, drain=0):
        """
        Read from the file descriptor. If 'drain' set, read until EOF.
        """
        while self._read() is not None:
            if not drain:
                break

    def _read(self):
        """
        Read up to 4k of data from this output stream. Collect the output
        up to the last newline, and append it to any leftover data from a
        previous call. The lines are stored as a (timestamp, data) tuple
        for easy sorting/merging later.
        """
        fd = self.fileno()
        buf = os.read(fd, 4096)
        if not buf:
            return None
        if '\n' not in buf:
            self._buf += buf
            return []

        buf = self._buf + buf
        tmp, rest = buf.rsplit('\n', 1)
        self._buf = rest
        now = datetime.now()
        rows = tmp.split('\n')
        self.lines += [(now, r) for r in rows]


class Cmd(object):
    verified_users = []

    def __init__(self, pathname, outputdir=None, timeout=None, user=None,
                 tags=None):
        self.pathname = pathname
        self.outputdir = outputdir or 'BASEDIR'
        self.timeout = timeout
        self.user = user or ''
        self.killed = False
        self.result = Result()

        if self.timeout is None:
            self.timeout = 60

    def __str__(self):
        return "Pathname: %s\nOutputdir: %s\nTimeout: %d\nUser: %s\n" % \
            (self.pathname, self.outputdir, self.timeout, self.user)

    def kill_cmd(self, proc):
        """
        Kill a running command due to timeout, or ^C from the keyboard. If
        sudo is required, this user was verified previously.
        """
        self.killed = True
        do_sudo = len(self.user) != 0
        signal = '-TERM'

        cmd = [SUDO, KILL, signal, str(proc.pid)]
        if not do_sudo:
            del cmd[0]

        try:
            kp = Popen(cmd)
            kp.wait()
        except Exception:
            pass

    def update_cmd_privs(self, cmd, user):
        """
        If a user has been specified to run this Cmd and we're not already
        running as that user, prepend the appropriate sudo command to run
        as that user.
        """
        me = getpwuid(os.getuid())

        if not user or user is me:
            if os.path.isfile(cmd+'.ksh') and os.access(cmd+'.ksh', os.X_OK):
                cmd += '.ksh'
            if os.path.isfile(cmd+'.sh') and os.access(cmd+'.sh', os.X_OK):
                cmd += '.sh'
            return cmd

        if not os.path.isfile(cmd):
            if os.path.isfile(cmd+'.ksh') and os.access(cmd+'.ksh', os.X_OK):
                cmd += '.ksh'
            if os.path.isfile(cmd+'.sh') and os.access(cmd+'.sh', os.X_OK):
                cmd += '.sh'

        ret = '%s -E -u %s %s' % (SUDO, user, cmd)
        return ret.split(' ')

    def collect_output(self, proc):
        """
        Read from stdout/stderr as data becomes available, until the
        process is no longer running. Return the lines from the stdout and
        stderr Output objects.
        """
        out = Output(proc.stdout)
        err = Output(proc.stderr)
        res = []
        while proc.returncode is None:
            proc.poll()
            res = select([out, err], [], [], .1)
            for fd in res[0]:
                fd.read()
        for fd in res[0]:
            fd.read(drain=1)

        return out.lines, err.lines

    def run(self, options):
        """
        This is the main function that runs each individual test.
        Determine whether or not the command requires sudo, and modify it
        if needed. Run the command, and update the result object.
        """
        if options.dryrun is True:
            print(self)
            return

        privcmd = self.update_cmd_privs(self.pathname, self.user)
        try:
            old = os.umask(0)
            if not os.path.isdir(self.outputdir):
                os.makedirs(self.outputdir, mode=0o777)
            os.umask(old)
        except OSError as e:
            fail('%s' % e)

        self.result.starttime = time()
        proc = Popen(privcmd, stdout=PIPE, stderr=PIPE)
        # Allow a special timeout value of 0 to mean infinity
        if int(self.timeout) == 0:
            self.timeout = maxsize
        t = Timer(int(self.timeout), self.kill_cmd, [proc])

        try:
            t.start()
            self.result.stdout, self.result.stderr = self.collect_output(proc)
        except KeyboardInterrupt:
            self.kill_cmd(proc)
            fail('\nRun terminated at user request.')
        finally:
            t.cancel()

        self.result.done(proc, self.killed)

    def skip(self):
        """
        Initialize enough of the test result that we can log a skipped
        command.
        """
        Result.total += 1
        Result.runresults['SKIP'] += 1
        self.result.stdout = self.result.stderr = []
        self.result.starttime = time()
        m, s = divmod(time() - self.result.starttime, 60)
        self.result.runtime = '%02d:%02d' % (m, s)
        self.result.result = 'SKIP'

    def log(self, logger, options):
        """
        This function is responsible for writing all output. This includes
        the console output, the logfile of all results (with timestamped
        merged stdout and stderr), and for each test, the unmodified
        stdout/stderr/merged in it's own file.
        """
        if logger is None:
            return

        logname = getpwuid(os.getuid()).pw_name
        user = ' (run as %s)' % (self.user if len(self.user) else logname)
        msga = 'Test: %s%s ' % (self.pathname, user)
        msgb = '[%s] [%s]' % (self.result.runtime, self.result.result)
        pad = ' ' * (80 - (len(msga) + len(msgb)))

        # If -q is specified, only print a line for tests that didn't pass.
        # This means passing tests need to be logged as DEBUG, or the one
        # line summary will only be printed in the logfile for failures.
        if not options.quiet:
            logger.info('%s%s%s' % (msga, pad, msgb))
        elif self.result.result is not 'PASS':
            logger.info('%s%s%s' % (msga, pad, msgb))
        else:
            logger.debug('%s%s%s' % (msga, pad, msgb))

        lines = sorted(self.result.stdout + self.result.stderr,
                       key=lambda x: x[0])

        for dt, line in lines:
            logger.debug('%s %s' % (dt.strftime("%H:%M:%S.%f ")[:11], line))

        if len(self.result.stdout):
            with open(os.path.join(self.outputdir, 'stdout'), 'w') as out:
                for _, line in self.result.stdout:
                    os.write(out.fileno(), '%s\n' % line)
        if len(self.result.stderr):
            with open(os.path.join(self.outputdir, 'stderr'), 'w') as err:
                for _, line in self.result.stderr:
                    os.write(err.fileno(), '%s\n' % line)
        if len(self.result.stdout) and len(self.result.stderr):
            with open(os.path.join(self.outputdir, 'merged'), 'w') as merged:
                for _, line in lines:
                    os.write(merged.fileno(), '%s\n' % line)


class Test(Cmd):
    props = ['outputdir', 'timeout', 'user', 'pre', 'pre_user', 'post',
             'post_user', 'tags']

    def __init__(self, pathname, outputdir=None, timeout=None, user=None,
                 pre=None, pre_user=None, post=None, post_user=None,
                 tags=None):
        super(Test, self).__init__(pathname, outputdir, timeout, user)
        self.pre = pre or ''
        self.pre_user = pre_user or ''
        self.post = post or ''
        self.post_user = post_user or ''
        self.tags = tags or []

    def __str__(self):
        post_user = pre_user = ''
        if len(self.pre_user):
            pre_user = ' (as %s)' % (self.pre_user)
        if len(self.post_user):
            post_user = ' (as %s)' % (self.post_user)
        return "Pathname: %s\nOutputdir: %s\nTimeout: %d\nPre: %s%s\nPost: " \
               "%s%s\nUser: %s\nTags: %s\n" % \
               (self.pathname, self.outputdir, self.timeout, self.pre,
                pre_user, self.post, post_user, self.user, self.tags)

    def verify(self, logger):
        """
        Check the pre/post scripts, user and Test. Omit the Test from this
        run if there are any problems.
        """
        files = [self.pre, self.pathname, self.post]
        users = [self.pre_user, self.user, self.post_user]

        for f in [f for f in files if len(f)]:
            if not verify_file(f):
                logger.info("Warning: Test '%s' not added to this run because"
                            " it failed verification." % f)
                return False

        for user in [user for user in users if len(user)]:
            if not verify_user(user, logger):
                logger.info("Not adding Test '%s' to this run." %
                            self.pathname)
                return False

        return True

    def run(self, logger, options):
        """
        Create Cmd instances for the pre/post scripts. If the pre script
        doesn't pass, skip this Test. Run the post script regardless.
        """
        odir = os.path.join(self.outputdir, os.path.basename(self.pre))
        pretest = Cmd(self.pre, outputdir=odir, timeout=self.timeout,
                      user=self.pre_user)
        test = Cmd(self.pathname, outputdir=self.outputdir,
                   timeout=self.timeout, user=self.user)
        odir = os.path.join(self.outputdir, os.path.basename(self.post))
        posttest = Cmd(self.post, outputdir=odir, timeout=self.timeout,
                       user=self.post_user)

        cont = True
        if len(pretest.pathname):
            pretest.run(options)
            cont = pretest.result.result is 'PASS'
            pretest.log(logger, options)

        if cont:
            test.run(options)
        else:
            test.skip()

        test.log(logger, options)

        if len(posttest.pathname):
            posttest.run(options)
            posttest.log(logger, options)


class TestGroup(Test):
    props = Test.props + ['tests']

    def __init__(self, pathname, outputdir=None, timeout=None, user=None,
                 pre=None, pre_user=None, post=None, post_user=None,
                 tests=None, tags=None):
        super(TestGroup, self).__init__(pathname, outputdir, timeout, user,
                                        pre, pre_user, post, post_user, tags)
        self.tests = tests or []

    def __str__(self):
        post_user = pre_user = ''
        if len(self.pre_user):
            pre_user = ' (as %s)' % (self.pre_user)
        if len(self.post_user):
            post_user = ' (as %s)' % (self.post_user)
        return "Pathname: %s\nOutputdir: %s\nTests: %s\nTimeout: %s\n" \
               "Pre: %s%s\nPost: %s%s\nUser: %s\nTags: %s\n" % \
               (self.pathname, self.outputdir, self.tests, self.timeout,
                self.pre, pre_user, self.post, post_user, self.user, self.tags)

    def verify(self, logger):
        """
        Check the pre/post scripts, user and tests in this TestGroup. Omit
        the TestGroup entirely, or simply delete the relevant tests in the
        group, if that's all that's required.
        """
        # If the pre or post scripts are relative pathnames, convert to
        # absolute, so they stand a chance of passing verification.
        if len(self.pre) and not os.path.isabs(self.pre):
            self.pre = os.path.join(self.pathname, self.pre)
        if len(self.post) and not os.path.isabs(self.post):
            self.post = os.path.join(self.pathname, self.post)

        auxfiles = [self.pre, self.post]
        users = [self.pre_user, self.user, self.post_user]

        for f in [f for f in auxfiles if len(f)]:
            if self.pathname != os.path.dirname(f):
                logger.info("Warning: TestGroup '%s' not added to this run. "
                            "Auxiliary script '%s' exists in a different "
                            "directory." % (self.pathname, f))
                return False

            if not verify_file(f):
                logger.info("Warning: TestGroup '%s' not added to this run. "
                            "Auxiliary script '%s' failed verification." %
                            (self.pathname, f))
                return False

        for user in [user for user in users if len(user)]:
            if not verify_user(user, logger):
                logger.info("Not adding TestGroup '%s' to this run." %
                            self.pathname)
                return False

        # If one of the tests is invalid, delete it, log it, and drive on.
        for test in self.tests:
            if not verify_file(os.path.join(self.pathname, test)):
                del self.tests[self.tests.index(test)]
                logger.info("Warning: Test '%s' removed from TestGroup '%s' "
                            "because it failed verification." %
                            (test, self.pathname))

        return len(self.tests) is not 0

    def run(self, logger, options):
        """
        Create Cmd instances for the pre/post scripts. If the pre script
        doesn't pass, skip all the tests in this TestGroup. Run the post
        script regardless.
        """
        # tags assigned to this test group also include the test names
        if options.tags and not set(self.tags).intersection(set(options.tags)):
            return

        odir = os.path.join(self.outputdir, os.path.basename(self.pre))
        pretest = Cmd(self.pre, outputdir=odir, timeout=self.timeout,
                      user=self.pre_user)
        odir = os.path.join(self.outputdir, os.path.basename(self.post))
        posttest = Cmd(self.post, outputdir=odir, timeout=self.timeout,
                       user=self.post_user)

        cont = True
        if len(pretest.pathname):
            pretest.run(options)
            cont = pretest.result.result is 'PASS'
            pretest.log(logger, options)

        for fname in self.tests:
            test = Cmd(os.path.join(self.pathname, fname),
                       outputdir=os.path.join(self.outputdir, fname),
                       timeout=self.timeout, user=self.user)
            if cont:
                test.run(options)
            else:
                test.skip()

            test.log(logger, options)

        if len(posttest.pathname):
            posttest.run(options)
            posttest.log(logger, options)


class TestRun(object):
    props = ['quiet', 'outputdir']

    def __init__(self, options):
        self.tests = {}
        self.testgroups = {}
        self.starttime = time()
        self.timestamp = datetime.now().strftime('%Y%m%dT%H%M%S')
        self.outputdir = os.path.join(options.outputdir, self.timestamp)
        self.logger = self.setup_logging(options)
        self.defaults = [
            ('outputdir', BASEDIR),
            ('quiet', False),
            ('timeout', 60),
            ('user', ''),
            ('pre', ''),
            ('pre_user', ''),
            ('post', ''),
            ('post_user', ''),
            ('tags', [])
        ]

    def __str__(self):
        s = 'TestRun:\n    outputdir: %s\n' % self.outputdir
        s += 'TESTS:\n'
        for key in sorted(self.tests.keys()):
            s += '%s%s' % (self.tests[key].__str__(), '\n')
        s += 'TESTGROUPS:\n'
        for key in sorted(self.testgroups.keys()):
            s += '%s%s' % (self.testgroups[key].__str__(), '\n')
        return s

    def addtest(self, pathname, options):
        """
        Create a new Test, and apply any properties that were passed in
        from the command line. If it passes verification, add it to the
        TestRun.
        """
        test = Test(pathname)
        for prop in Test.props:
            setattr(test, prop, getattr(options, prop))

        if test.verify(self.logger):
            self.tests[pathname] = test

    def addtestgroup(self, dirname, filenames, options):
        """
        Create a new TestGroup, and apply any properties that were passed
        in from the command line. If it passes verification, add it to the
        TestRun.
        """
        if dirname not in self.testgroups:
            testgroup = TestGroup(dirname)
            for prop in Test.props:
                setattr(testgroup, prop, getattr(options, prop))

            # Prevent pre/post scripts from running as regular tests
            for f in [testgroup.pre, testgroup.post]:
                if f in filenames:
                    del filenames[filenames.index(f)]

            self.testgroups[dirname] = testgroup
            self.testgroups[dirname].tests = sorted(filenames)

            testgroup.verify(self.logger)

    def read(self, logger, options):
        """
        Read in the specified runfile, and apply the TestRun properties
        listed in the 'DEFAULT' section to our TestRun. Then read each
        section, and apply the appropriate properties to the Test or
        TestGroup. Properties from individual sections override those set
        in the 'DEFAULT' section. If the Test or TestGroup passes
        verification, add it to the TestRun.
        """
        config = configparser.RawConfigParser()
        if not len(config.read(options.runfile)):
            fail("Coulnd't read config file %s" % options.runfile)

        for opt in TestRun.props:
            if config.has_option('DEFAULT', opt):
                setattr(self, opt, config.get('DEFAULT', opt))
        self.outputdir = os.path.join(self.outputdir, self.timestamp)

        for section in config.sections():
            if 'tests' in config.options(section):
                if os.path.isdir(section):
                    pathname = section
                elif os.path.isdir(os.path.join(options.testdir, section)):
                    pathname = os.path.join(options.testdir, section)
                else:
                    pathname = section

                testgroup = TestGroup(os.path.abspath(pathname))
                for prop in TestGroup.props:
                    for sect in ['DEFAULT', section]:
                        if config.has_option(sect, prop):
                            if prop is "tags":
                                setattr(testgroup, prop,
                                        eval(config.get(sect, prop)))
                            else:
                                setattr(testgroup, prop,
                                        config.get(sect, prop))

                # Repopulate tests using eval to convert the string to a list
                testgroup.tests = eval(config.get(section, 'tests'))

                if testgroup.verify(logger):
                    self.testgroups[section] = testgroup
            else:
                test = Test(section)
                for prop in Test.props:
                    for sect in ['DEFAULT', section]:
                        if config.has_option(sect, prop):
                            setattr(test, prop, config.get(sect, prop))

                if test.verify(logger):
                    self.tests[section] = test

    def write(self, options):
        """
        Create a configuration file for editing and later use. The
        'DEFAULT' section of the config file is created from the
        properties that were specified on the command line. Tests are
        simply added as sections that inherit everything from the
        'DEFAULT' section. TestGroups are the same, except they get an
        option including all the tests to run in that directory.
        """

        defaults = dict([(prop, getattr(options, prop)) for prop, _ in
                         self.defaults])
        config = configparser.RawConfigParser(defaults)

        for test in sorted(self.tests.keys()):
            config.add_section(test)

        for testgroup in sorted(self.testgroups.keys()):
            config.add_section(testgroup)
            config.set(testgroup, 'tests', self.testgroups[testgroup].tests)

        try:
            with open(options.template, 'w') as f:
                return config.write(f)
        except IOError:
            fail('Could not open \'%s\' for writing.' % options.template)

    def complete_outputdirs(self):
        """
        Collect all the pathnames for Tests, and TestGroups. Work
        backwards one pathname component at a time, to create a unique
        directory name in which to deposit test output. Tests will be able
        to write output files directly in the newly modified outputdir.
        TestGroups will be able to create one subdirectory per test in the
        outputdir, and are guaranteed uniqueness because a group can only
        contain files in one directory. Pre and post tests will create a
        directory rooted at the outputdir of the Test or TestGroup in
        question for their output.
        """
        done = False
        components = 0
        tmp_dict = dict(list(self.tests.items()) +
                        list(self.testgroups.items()))
        total = len(tmp_dict)
        base = self.outputdir

        while not done:
            paths = []
            components -= 1
            for testfile in list(tmp_dict.keys()):
                uniq = '/'.join(testfile.split('/')[components:]).lstrip('/')
                if uniq not in paths:
                    paths.append(uniq)
                    tmp_dict[testfile].outputdir = os.path.join(base, uniq)
                else:
                    break
            done = total == len(paths)

    def setup_logging(self, options):
        """
        Two loggers are set up here. The first is for the logfile which
        will contain one line summarizing the test, including the test
        name, result, and running time. This logger will also capture the
        timestamped combined stdout and stderr of each run. The second
        logger is optional console output, which will contain only the one
        line summary. The loggers are initialized at two different levels
        to facilitate segregating the output.
        """
        if options.dryrun is True:
            return

        testlogger = logging.getLogger(__name__)
        testlogger.setLevel(logging.DEBUG)

        if options.cmd is not 'wrconfig':
            try:
                old = os.umask(0)
                os.makedirs(self.outputdir, mode=0o777)
                os.umask(old)
            except OSError as e:
                fail('%s' % e)
            filename = os.path.join(self.outputdir, 'log')

            logfile = logging.FileHandler(filename)
            logfile.setLevel(logging.DEBUG)
            logfilefmt = logging.Formatter('%(message)s')
            logfile.setFormatter(logfilefmt)
            testlogger.addHandler(logfile)

        cons = logging.StreamHandler()
        cons.setLevel(logging.INFO)
        consfmt = logging.Formatter('%(message)s')
        cons.setFormatter(consfmt)
        testlogger.addHandler(cons)

        return testlogger

    def run(self, options):
        """
        Walk through all the Tests and TestGroups, calling run().
        """
        try:
            os.chdir(self.outputdir)
        except OSError:
            fail('Could not change to directory %s' % self.outputdir)
        # make a symlink to the output for the currently running test
        logsymlink = os.path.join(self.outputdir, '../current')
        if os.path.islink(logsymlink):
            os.unlink(logsymlink)
        if not os.path.exists(logsymlink):
            os.symlink(self.outputdir, logsymlink)
        else:
            print('Could not make a symlink to directory %s' % (
                self.outputdir))
        iteration = 0
        while iteration < options.iterations:
            for test in sorted(self.tests.keys()):
                self.tests[test].run(self.logger, options)
            for testgroup in sorted(self.testgroups.keys()):
                self.testgroups[testgroup].run(self.logger, options)
            iteration += 1

    def summary(self):
        if Result.total is 0:
            return 2

        print('\nResults Summary')
        for key in list(Result.runresults.keys()):
            if Result.runresults[key] is not 0:
                print('%s\t% 4d' % (key, Result.runresults[key]))

        m, s = divmod(time() - self.starttime, 60)
        h, m = divmod(m, 60)
        print('\nRunning Time:\t%02d:%02d:%02d' % (h, m, s))
        print('Percent passed:\t%.1f%%' % ((float(Result.runresults['PASS']) /
                                            float(Result.total)) * 100))
        print('Log directory:\t%s' % self.outputdir)

        if Result.runresults['FAIL'] > 0:
            return 1

        if Result.runresults['KILLED'] > 0:
            return 1

        return 0


def verify_file(pathname):
    """
    Verify that the supplied pathname is an executable regular file.
    """
    if os.path.isdir(pathname) or os.path.islink(pathname):
        return False

    for ext in '', '.ksh', '.sh':
        script_path = pathname + ext
        if os.path.isfile(script_path) and os.access(script_path, os.X_OK):
            return True

    return False


def verify_user(user, logger):
    """
    Verify that the specified user exists on this system, and can execute
    sudo without being prompted for a password.
    """
    testcmd = [SUDO, '-n', '-u', user, TRUE]

    if user in Cmd.verified_users:
        return True

    try:
        getpwnam(user)
    except KeyError:
        logger.info("Warning: user '%s' does not exist.", user)
        return False

    p = Popen(testcmd)
    p.wait()
    if p.returncode is not 0:
        logger.info("Warning: user '%s' cannot use passwordless sudo.", user)
        return False
    else:
        Cmd.verified_users.append(user)

    return True


def find_tests(testrun, options):
    """
    For the given list of pathnames, add files as Tests. For directories,
    if do_groups is True, add the directory as a TestGroup. If False,
    recursively search for executable files.
    """

    for p in sorted(options.pathnames):
        if os.path.isdir(p):
            for dirname, _, filenames in os.walk(p):
                if options.do_groups:
                    testrun.addtestgroup(dirname, filenames, options)
                else:
                    for f in sorted(filenames):
                        testrun.addtest(os.path.join(dirname, f), options)
        else:
            testrun.addtest(p, options)


def fail(retstr, ret=1):
    print('%s: %s' % (argv[0], retstr))
    exit(ret)


def options_cb(option, opt_str, value, parser):
    path_options = ['runfile', 'outputdir', 'template', 'testdir']

    if option.dest is 'runfile' and '-w' in parser.rargs or \
            option.dest is 'template' and '-c' in parser.rargs:
        fail('-c and -w are mutually exclusive.')

    if opt_str in parser.rargs:
        fail('%s may only be specified once.' % opt_str)

    if option.dest is 'runfile':
        parser.values.cmd = 'rdconfig'
    if option.dest is 'template':
        parser.values.cmd = 'wrconfig'
    if option.dest is 'tags':
        value = [x.strip() for x in value.split(',')]

    setattr(parser.values, option.dest, value)
    if option.dest in path_options:
        setattr(parser.values, option.dest, os.path.abspath(value))


def parse_args():
    parser = OptionParser()
    parser.add_option('-c', action='callback', callback=options_cb,
                      type='string', dest='runfile', metavar='runfile',
                      help='Specify tests to run via config file.')
    parser.add_option('-d', action='store_true', default=False, dest='dryrun',
                      help='Dry run. Print tests, but take no other action.')
    parser.add_option('-g', action='store_true', default=False,
                      dest='do_groups', help='Make directories TestGroups.')
    parser.add_option('-o', action='callback', callback=options_cb,
                      default=BASEDIR, dest='outputdir', type='string',
                      metavar='outputdir', help='Specify an output directory.')
    parser.add_option('-i', action='callback', callback=options_cb,
                      default=TESTDIR, dest='testdir', type='string',
                      metavar='testdir', help='Specify a test directory.')
    parser.add_option('-p', action='callback', callback=options_cb,
                      default='', dest='pre', metavar='script',
                      type='string', help='Specify a pre script.')
    parser.add_option('-P', action='callback', callback=options_cb,
                      default='', dest='post', metavar='script',
                      type='string', help='Specify a post script.')
    parser.add_option('-q', action='store_true', default=False, dest='quiet',
                      help='Silence on the console during a test run.')
    parser.add_option('-t', action='callback', callback=options_cb, default=60,
                      dest='timeout', metavar='seconds', type='int',
                      help='Timeout (in seconds) for an individual test.')
    parser.add_option('-u', action='callback', callback=options_cb,
                      default='', dest='user', metavar='user', type='string',
                      help='Specify a different user name to run as.')
    parser.add_option('-w', action='callback', callback=options_cb,
                      default=None, dest='template', metavar='template',
                      type='string', help='Create a new config file.')
    parser.add_option('-x', action='callback', callback=options_cb, default='',
                      dest='pre_user', metavar='pre_user', type='string',
                      help='Specify a user to execute the pre script.')
    parser.add_option('-X', action='callback', callback=options_cb, default='',
                      dest='post_user', metavar='post_user', type='string',
                      help='Specify a user to execute the post script.')
    parser.add_option('-T', action='callback', callback=options_cb, default='',
                      dest='tags', metavar='tags', type='string',
                      help='Specify tags to execute specific test groups.')
    parser.add_option('-I', action='callback', callback=options_cb, default=1,
                      dest='iterations', metavar='iterations', type='int',
                      help='Number of times to run the test run.')
    (options, pathnames) = parser.parse_args()

    if not options.runfile and not options.template:
        options.cmd = 'runtests'

    if options.runfile and len(pathnames):
        fail('Extraneous arguments.')

    options.pathnames = [os.path.abspath(path) for path in pathnames]

    return options


def main():
    options = parse_args()
    testrun = TestRun(options)

    if options.cmd is 'runtests':
        find_tests(testrun, options)
    elif options.cmd is 'rdconfig':
        testrun.read(testrun.logger, options)
    elif options.cmd is 'wrconfig':
        find_tests(testrun, options)
        testrun.write(options)
        exit(0)
    else:
        fail('Unknown command specified')

    testrun.complete_outputdirs()
    testrun.run(options)
    exit(testrun.summary())


if __name__ == '__main__':
    main()
