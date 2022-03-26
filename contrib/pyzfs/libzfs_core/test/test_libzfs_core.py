#
# Copyright 2015 ClusterHQ
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
Tests for `libzfs_core` operations.

These are mostly functional and conformance tests that validate
that the operations produce expected effects or fail with expected
exceptions.
"""
from __future__ import absolute_import, division, print_function

import unittest
import contextlib
import errno
import filecmp
import os
import platform
import resource
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import uuid
import itertools
import zlib
from .. import _libzfs_core as lzc
from .. import exceptions as lzc_exc
from .._nvlist import packed_nvlist_out


def _print(*args):
    for arg in args:
        print(arg, end=' ')
    print()


@contextlib.contextmanager
def suppress(exceptions=None):
    try:
        yield
    except BaseException as e:
        if exceptions is None or isinstance(e, exceptions):
            pass
        else:
            raise


@contextlib.contextmanager
def _zfs_mount(fs):
    mntdir = tempfile.mkdtemp()
    if platform.system() == 'SunOS':
        mount_cmd = ['mount', '-F', 'zfs', fs, mntdir]
    else:
        mount_cmd = ['mount', '-t', 'zfs', fs, mntdir]
    unmount_cmd = ['umount', '-f', mntdir]

    try:
        subprocess.check_output(mount_cmd, stderr=subprocess.STDOUT)
        try:
            yield mntdir
        finally:
            with suppress():
                subprocess.check_output(unmount_cmd, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        print('failed to mount %s @ %s : %s' % (fs, mntdir, e.output))
        raise
    finally:
        os.rmdir(mntdir)


# XXX On illumos it is impossible to explicitly mount a snapshot.
# So, either we need to implicitly mount it using .zfs/snapshot/
# or we need to create a clone and mount it readonly (and discard
# it afterwards).
# At the moment the former approach is implemented.

# This dictionary is used to keep track of mounted filesystems
# (not snapshots), so that we do not try to mount a filesystem
# more than once in the case more than one snapshot of the
# filesystem is accessed from the same context or the filesystem
# and its snapshot are accessed.
_mnttab = {}


@contextlib.contextmanager
def _illumos_mount_fs(fs):
    if fs in _mnttab:
        yield _mnttab[fs]
    else:
        with _zfs_mount(fs) as mntdir:
            _mnttab[fs] = mntdir
            try:
                yield mntdir
            finally:
                _mnttab.pop(fs, None)


@contextlib.contextmanager
def _illumos_mount_snap(fs):
    (base, snap) = fs.split('@', 1)
    with _illumos_mount_fs(base) as mntdir:
        yield os.path.join(mntdir, '.zfs', 'snapshot', snap)


@contextlib.contextmanager
def _zfs_mount_illumos(fs):
    if '@' not in fs:
        with _illumos_mount_fs(fs) as mntdir:
            yield mntdir
    else:
        with _illumos_mount_snap(fs) as mntdir:
            yield mntdir


if platform.system() == 'SunOS':
    zfs_mount = _zfs_mount_illumos
else:
    zfs_mount = _zfs_mount


@contextlib.contextmanager
def cleanup_fd():
    fd = os.open('/dev/zfs', os.O_EXCL)
    try:
        yield fd
    finally:
        os.close(fd)


@contextlib.contextmanager
def os_open(name, mode):
    fd = os.open(name, mode)
    try:
        yield fd
    finally:
        os.close(fd)


@contextlib.contextmanager
def dev_null():
    with tempfile.TemporaryFile(suffix='.zstream') as fd:
        yield fd.fileno()


@contextlib.contextmanager
def dev_zero():
    with os_open('/dev/zero', os.O_RDONLY) as fd:
        yield fd


@contextlib.contextmanager
def temp_file_in_fs(fs):
    with zfs_mount(fs) as mntdir:
        with tempfile.NamedTemporaryFile(dir=mntdir) as f:
            for i in range(1024):
                f.write(b'x' * 1024)
            f.flush()
            yield f.name


def make_snapshots(fs, before, modified, after):
    def _maybe_snap(snap):
        if snap is not None:
            if not snap.startswith(fs):
                snap = fs + b'@' + snap
            lzc.lzc_snapshot([snap])
        return snap

    before = _maybe_snap(before)
    with temp_file_in_fs(fs) as name:
        modified = _maybe_snap(modified)
    after = _maybe_snap(after)

    return (name, (before, modified, after))


@contextlib.contextmanager
def streams(fs, first, second):
    (filename, snaps) = make_snapshots(fs, None, first, second)
    with tempfile.TemporaryFile(suffix='.zstream') as full:
        lzc.lzc_send(snaps[1], None, full.fileno())
        full.seek(0)
        if snaps[2] is not None:
            with tempfile.TemporaryFile(suffix='.zstream') as incremental:
                lzc.lzc_send(snaps[2], snaps[1], incremental.fileno())
                incremental.seek(0)
                yield (filename, (full, incremental))
        else:
            yield (filename, (full, None))


@contextlib.contextmanager
def encrypted_filesystem():
    fs = ZFSTest.pool.getFilesystem(b"encrypted")
    name = fs.getName()
    filename = None
    key = os.urandom(lzc.WRAPPING_KEY_LEN)
    with tempfile.NamedTemporaryFile() as f:
        filename = "file://" + f.name
        props = {
            b"encryption": lzc.zio_encrypt.ZIO_CRYPT_AES_256_CCM,
            b"keylocation": filename.encode(),
            b"keyformat": lzc.zfs_keyformat.ZFS_KEYFORMAT_RAW,
        }
        lzc.lzc_create(name, 'zfs', props=props, key=key)
    yield (name, key)


def runtimeSkipIf(check_method, message):
    def _decorator(f):
        def _f(_self, *args, **kwargs):
            if check_method(_self):
                return _self.skipTest(message)
            else:
                return f(_self, *args, **kwargs)
        _f.__name__ = f.__name__
        return _f
    return _decorator


def skipIfFeatureAvailable(feature, message):
    return runtimeSkipIf(
        lambda _self: _self.__class__.pool.isPoolFeatureAvailable(feature),
        message)


def skipUnlessFeatureEnabled(feature, message):
    return runtimeSkipIf(
        lambda _self: not _self.__class__.pool.isPoolFeatureEnabled(feature),
        message)


def skipUnlessBookmarksSupported(f):
    return skipUnlessFeatureEnabled(
        'bookmarks', 'bookmarks are not enabled')(f)


def snap_always_unmounted_before_destruction():
    # Apparently OpenZFS automatically unmounts the snapshot
    # only if it is mounted at its default .zfs/snapshot
    # mountpoint under Linux.
    return (
        platform.system() != 'Linux', 'snapshot is not auto-unmounted')


def illumos_bug_6379():
    # zfs_ioc_hold() panics on a bad cleanup fd
    return (
        platform.system() == 'SunOS',
        'see https://www.illumos.org/issues/6379')


def needs_support(function):
    return unittest.skipUnless(
        lzc.is_supported(function),
        '{} not available'.format(function.__name__))


class ZFSTest(unittest.TestCase):
    POOL_FILE_SIZE = 128 * 1024 * 1024
    FILESYSTEMS = [b'fs1', b'fs2', b'fs1/fs']

    pool = None
    misc_pool = None
    readonly_pool = None

    @classmethod
    def setUpClass(cls):
        try:
            cls.pool = _TempPool(filesystems=cls.FILESYSTEMS)
            cls.misc_pool = _TempPool()
            cls.readonly_pool = _TempPool(
                filesystems=cls.FILESYSTEMS, readonly=True)
            cls.pools = [cls.pool, cls.misc_pool, cls.readonly_pool]
        except Exception:
            cls._cleanUp()
            raise

    @classmethod
    def tearDownClass(cls):
        cls._cleanUp()

    @classmethod
    def _cleanUp(cls):
        for pool in [cls.pool, cls.misc_pool, cls.readonly_pool]:
            if pool is not None:
                pool.cleanUp()

    def setUp(self):
        pass

    def tearDown(self):
        for pool in ZFSTest.pools:
            pool.reset()

    def assertExists(self, name):
        self.assertTrue(
            lzc.lzc_exists(name), 'ZFS dataset %s does not exist' % (name, ))

    def assertNotExists(self, name):
        self.assertFalse(
            lzc.lzc_exists(name), 'ZFS dataset %s exists' % (name, ))

    def test_exists(self):
        self.assertExists(ZFSTest.pool.makeName())

    def test_exists_in_ro_pool(self):
        self.assertExists(ZFSTest.readonly_pool.makeName())

    def test_exists_failure(self):
        self.assertNotExists(ZFSTest.pool.makeName(b'nonexistent'))

    def test_create_fs(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test1")

        lzc.lzc_create(name)
        self.assertExists(name)

    def test_create_zvol(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/zvol")
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(name, ds_type='zvol', props=props)
        self.assertExists(name)
        # On Gentoo with ZFS 0.6.5.4 the volume is busy
        # and can not be destroyed right after its creation.
        # A reason for this is unknown at the moment.
        # Because of that the post-test clean up could fail.
        time.sleep(0.1)

    def test_create_fs_with_prop(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test2")
        props = {b"atime": 0}

        lzc.lzc_create(name, props=props)
        self.assertExists(name)

    def test_create_fs_wrong_ds_type(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test1")

        with self.assertRaises(lzc_exc.DatasetTypeInvalid):
            lzc.lzc_create(name, ds_type='wrong')

    def test_create_fs_below_zvol(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/zvol")
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(name, ds_type='zvol', props=props)
        with self.assertRaises(lzc_exc.WrongParent):
            lzc.lzc_create(name + b'/fs')

    def test_create_zvol_below_zvol(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/zvol")
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(name, ds_type='zvol', props=props)
        with self.assertRaises(lzc_exc.WrongParent):
            lzc.lzc_create(name + b'/zvol', ds_type='zvol', props=props)

    def test_create_fs_duplicate(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test6")

        lzc.lzc_create(name)

        with self.assertRaises(lzc_exc.FilesystemExists):
            lzc.lzc_create(name)

    def test_create_fs_in_ro_pool(self):
        name = ZFSTest.readonly_pool.makeName(b"fs")

        with self.assertRaises(lzc_exc.ReadOnlyPool):
            lzc.lzc_create(name)

    def test_create_fs_without_parent(self):
        name = ZFSTest.pool.makeName(b"fs1/nonexistent/test")

        with self.assertRaises(lzc_exc.ParentNotFound):
            lzc.lzc_create(name)
        self.assertNotExists(name)

    def test_create_fs_in_nonexistent_pool(self):
        name = b"no-such-pool/fs"

        with self.assertRaises(lzc_exc.ParentNotFound):
            lzc.lzc_create(name)
        self.assertNotExists(name)

    def test_create_fs_with_invalid_prop(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test3")
        props = {b"BOGUS": 0}

        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_create(name, 'zfs', props)
        self.assertNotExists(name)

    def test_create_fs_with_invalid_prop_type(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test4")
        props = {b"recordsize": b"128k"}

        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_create(name, 'zfs', props)
        self.assertNotExists(name)

    def test_create_fs_with_invalid_prop_val(self):
        name = ZFSTest.pool.makeName(b"fs1/fs/test5")
        props = {b"atime": 20}

        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_create(name, 'zfs', props)
        self.assertNotExists(name)

    def test_create_fs_with_invalid_name(self):
        name = ZFSTest.pool.makeName(b"@badname")

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_create(name)
        self.assertNotExists(name)

    def test_create_fs_with_invalid_pool_name(self):
        name = b"bad!pool/fs"

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_create(name)
        self.assertNotExists(name)

    def test_create_encrypted_fs(self):
        fs = ZFSTest.pool.getFilesystem(b"encrypted")
        name = fs.getName()
        filename = None
        with tempfile.NamedTemporaryFile() as f:
            filename = "file://" + f.name
            props = {
                b"encryption": lzc.zio_encrypt.ZIO_CRYPT_AES_256_CCM,
                b"keylocation": filename.encode(),
                b"keyformat": lzc.zfs_keyformat.ZFS_KEYFORMAT_RAW,
            }
            key = os.urandom(lzc.WRAPPING_KEY_LEN)
            lzc.lzc_create(name, 'zfs', props=props, key=key)
        self.assertEqual(fs.getProperty("encryption"), b"aes-256-ccm")
        self.assertEqual(fs.getProperty("encryptionroot"), name)
        self.assertEqual(fs.getProperty("keylocation"), filename.encode())
        self.assertEqual(fs.getProperty("keyformat"), b"raw")

    def test_snapshot(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname]

        lzc.lzc_snapshot(snaps)
        self.assertExists(snapname)

    def test_snapshot_empty_list(self):
        lzc.lzc_snapshot([])

    def test_snapshot_user_props(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname]
        props = {b"user:foo": b"bar"}

        lzc.lzc_snapshot(snaps, props)
        self.assertExists(snapname)

    def test_snapshot_invalid_props(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname]
        props = {b"foo": b"bar"}

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps, props)

        self.assertEqual(len(ctx.exception.errors), len(snaps))
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PropertyInvalid)
        self.assertNotExists(snapname)

    def test_snapshot_ro_pool(self):
        snapname1 = ZFSTest.readonly_pool.makeName(b"@snap")
        snapname2 = ZFSTest.readonly_pool.makeName(b"fs1@snap")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # NB: one common error is reported.
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.ReadOnlyPool)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_snapshot_nonexistent_pool(self):
        snapname = b"no-such-pool@snap"
        snaps = [snapname]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.FilesystemNotFound)

    def test_snapshot_nonexistent_fs(self):
        snapname = ZFSTest.pool.makeName(b"nonexistent@snap")
        snaps = [snapname]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.FilesystemNotFound)

    def test_snapshot_nonexistent_and_existent_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.pool.makeName(b"nonexistent@snap")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.FilesystemNotFound)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_multiple_snapshots_nonexistent_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"nonexistent@snap1")
        snapname2 = ZFSTest.pool.makeName(b"nonexistent@snap2")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # XXX two errors should be reported but alas
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.DuplicateSnapshots)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_multiple_snapshots_multiple_nonexistent_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"nonexistent1@snap")
        snapname2 = ZFSTest.pool.makeName(b"nonexistent2@snap")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 2)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.FilesystemNotFound)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_snapshot_already_exists(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname]

        lzc.lzc_snapshot(snaps)

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotExists)

    def test_multiple_snapshots_for_same_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap1")
        snapname2 = ZFSTest.pool.makeName(b"@snap2")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.DuplicateSnapshots)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_multiple_snapshots(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.pool.makeName(b"fs1@snap")
        snaps = [snapname1, snapname2]

        lzc.lzc_snapshot(snaps)
        self.assertExists(snapname1)
        self.assertExists(snapname2)

    def test_multiple_existing_snapshots(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.pool.makeName(b"fs1@snap")
        snaps = [snapname1, snapname2]

        lzc.lzc_snapshot(snaps)

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 2)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotExists)

    def test_multiple_new_and_existing_snapshots(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.pool.makeName(b"fs1@snap")
        snapname3 = ZFSTest.pool.makeName(b"fs2@snap")
        snaps = [snapname1, snapname2]
        more_snaps = snaps + [snapname3]

        lzc.lzc_snapshot(snaps)

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(more_snaps)

        self.assertEqual(len(ctx.exception.errors), 2)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotExists)
        self.assertNotExists(snapname3)

    def test_snapshot_multiple_errors(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.pool.makeName(b"nonexistent@snap")
        snapname3 = ZFSTest.pool.makeName(b"fs1@snap")
        snaps = [snapname1]
        more_snaps = [snapname1, snapname2, snapname3]

        # create 'snapname1' snapshot
        lzc.lzc_snapshot(snaps)

        # attempt to create 3 snapshots:
        # 1. duplicate snapshot name
        # 2. refers to filesystem that doesn't exist
        # 3. could have succeeded if not for 1 and 2
        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(more_snaps)

        # It seems that FilesystemNotFound overrides the other error,
        # but it doesn't have to.
        self.assertGreater(len(ctx.exception.errors), 0)
        for e in ctx.exception.errors:
            self.assertIsInstance(
                e, (lzc_exc.SnapshotExists, lzc_exc.FilesystemNotFound))
        self.assertNotExists(snapname2)
        self.assertNotExists(snapname3)

    def test_snapshot_different_pools(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.misc_pool.makeName(b"@snap")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # NB: one common error is reported.
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolsDiffer)
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_snapshot_different_pools_ro_pool(self):
        snapname1 = ZFSTest.pool.makeName(b"@snap")
        snapname2 = ZFSTest.readonly_pool.makeName(b"@snap")
        snaps = [snapname1, snapname2]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # NB: one common error is reported.
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            # NB: depending on whether the first attempted snapshot is
            # for the read-only pool a different error is reported.
            self.assertIsInstance(
                e, (lzc_exc.PoolsDiffer, lzc_exc.ReadOnlyPool))
        self.assertNotExists(snapname1)
        self.assertNotExists(snapname2)

    def test_snapshot_invalid_name(self):
        snapname1 = ZFSTest.pool.makeName(b"@bad&name")
        snapname2 = ZFSTest.pool.makeName(b"fs1@bad*name")
        snapname3 = ZFSTest.pool.makeName(b"fs2@snap")
        snaps = [snapname1, snapname2, snapname3]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # NB: one common error is reported.
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)
            self.assertIsNone(e.name)

    def test_snapshot_too_long_complete_name(self):
        snapname1 = ZFSTest.pool.makeTooLongName(b"fs1@")
        snapname2 = ZFSTest.pool.makeTooLongName(b"fs2@")
        snapname3 = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname1, snapname2, snapname3]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        self.assertEqual(len(ctx.exception.errors), 2)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertIsNotNone(e.name)

    def test_snapshot_too_long_snap_name(self):
        snapname1 = ZFSTest.pool.makeTooLongComponent(b"fs1@")
        snapname2 = ZFSTest.pool.makeTooLongComponent(b"fs2@")
        snapname3 = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname1, snapname2, snapname3]

        with self.assertRaises(lzc_exc.SnapshotFailure) as ctx:
            lzc.lzc_snapshot(snaps)

        # NB: one common error is reported.
        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertIsNone(e.name)

    def test_destroy_nonexistent_snapshot(self):
        lzc.lzc_destroy_snaps([ZFSTest.pool.makeName(b"@nonexistent")], False)
        lzc.lzc_destroy_snaps([ZFSTest.pool.makeName(b"@nonexistent")], True)

    def test_destroy_snapshot_of_nonexistent_pool(self):
        with self.assertRaises(lzc_exc.SnapshotDestructionFailure) as ctx:
            lzc.lzc_destroy_snaps([b"no-such-pool@snap"], False)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolNotFound)

        with self.assertRaises(lzc_exc.SnapshotDestructionFailure) as ctx:
            lzc.lzc_destroy_snaps([b"no-such-pool@snap"], True)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolNotFound)

    # NB: note the difference from the nonexistent pool test.
    def test_destroy_snapshot_of_nonexistent_fs(self):
        lzc.lzc_destroy_snaps(
            [ZFSTest.pool.makeName(b"nonexistent@snap")], False)
        lzc.lzc_destroy_snaps(
            [ZFSTest.pool.makeName(b"nonexistent@snap")], True)

    # Apparently the name is not checked for validity.
    @unittest.expectedFailure
    def test_destroy_invalid_snap_name(self):
        with self.assertRaises(lzc_exc.SnapshotDestructionFailure):
            lzc.lzc_destroy_snaps(
                [ZFSTest.pool.makeName(b"@non$&*existent")], False)
        with self.assertRaises(lzc_exc.SnapshotDestructionFailure):
            lzc.lzc_destroy_snaps(
                [ZFSTest.pool.makeName(b"@non$&*existent")], True)

    # Apparently the full name is not checked for length.
    @unittest.expectedFailure
    def test_destroy_too_long_full_snap_name(self):
        snapname1 = ZFSTest.pool.makeTooLongName(b"fs1@")
        snaps = [snapname1]

        with self.assertRaises(lzc_exc.SnapshotDestructionFailure):
            lzc.lzc_destroy_snaps(snaps, False)
        with self.assertRaises(lzc_exc.SnapshotDestructionFailure):
            lzc.lzc_destroy_snaps(snaps, True)

    def test_destroy_too_long_short_snap_name(self):
        snapname1 = ZFSTest.pool.makeTooLongComponent(b"fs1@")
        snapname2 = ZFSTest.pool.makeTooLongComponent(b"fs2@")
        snapname3 = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname1, snapname2, snapname3]

        with self.assertRaises(lzc_exc.SnapshotDestructionFailure) as ctx:
            lzc.lzc_destroy_snaps(snaps, False)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)

    @unittest.skipUnless(*snap_always_unmounted_before_destruction())
    def test_destroy_mounted_snap(self):
        snap = ZFSTest.pool.getRoot().getSnap()

        lzc.lzc_snapshot([snap])
        with zfs_mount(snap):
            # the snapshot should be force-unmounted
            lzc.lzc_destroy_snaps([snap], defer=False)
            self.assertNotExists(snap)

    def test_clone(self):
        # NB: note the special name for the snapshot.
        # Since currently we can not destroy filesystems,
        # it would be impossible to destroy the snapshot,
        # so no point in attempting to clean it up.
        snapname = ZFSTest.pool.makeName(b"fs2@origin1")
        name = ZFSTest.pool.makeName(b"fs1/fs/clone1")

        lzc.lzc_snapshot([snapname])

        lzc.lzc_clone(name, snapname)
        self.assertExists(name)

    def test_clone_nonexistent_snapshot(self):
        snapname = ZFSTest.pool.makeName(b"fs2@nonexistent")
        name = ZFSTest.pool.makeName(b"fs1/fs/clone2")

        # XXX The error should be SnapshotNotFound
        # but limitations of C interface do not allow
        # to differentiate between the errors.
        with self.assertRaises(lzc_exc.DatasetNotFound):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_nonexistent_parent_fs(self):
        snapname = ZFSTest.pool.makeName(b"fs2@origin3")
        name = ZFSTest.pool.makeName(b"fs1/nonexistent/clone3")

        lzc.lzc_snapshot([snapname])

        with self.assertRaises(lzc_exc.DatasetNotFound):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_to_nonexistent_pool(self):
        snapname = ZFSTest.pool.makeName(b"fs2@snap")
        name = b"no-such-pool/fs"

        lzc.lzc_snapshot([snapname])

        with self.assertRaises(lzc_exc.DatasetNotFound):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_invalid_snap_name(self):
        # Use a valid filesystem name of filesystem that
        # exists as a snapshot name
        snapname = ZFSTest.pool.makeName(b"fs1/fs")
        name = ZFSTest.pool.makeName(b"fs2/clone")

        with self.assertRaises(lzc_exc.SnapshotNameInvalid):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_invalid_snap_name_2(self):
        # Use a valid filesystem name of filesystem that
        # doesn't exist as a snapshot name
        snapname = ZFSTest.pool.makeName(b"fs1/nonexistent")
        name = ZFSTest.pool.makeName(b"fs2/clone")

        with self.assertRaises(lzc_exc.SnapshotNameInvalid):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_invalid_name(self):
        snapname = ZFSTest.pool.makeName(b"fs2@snap")
        name = ZFSTest.pool.makeName(b"fs1/bad#name")

        lzc.lzc_snapshot([snapname])

        with self.assertRaises(lzc_exc.FilesystemNameInvalid):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_invalid_pool_name(self):
        snapname = ZFSTest.pool.makeName(b"fs2@snap")
        name = b"bad!pool/fs1"

        lzc.lzc_snapshot([snapname])

        with self.assertRaises(lzc_exc.FilesystemNameInvalid):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_across_pools(self):
        snapname = ZFSTest.pool.makeName(b"fs2@snap")
        name = ZFSTest.misc_pool.makeName(b"clone1")

        lzc.lzc_snapshot([snapname])

        with self.assertRaises(lzc_exc.PoolsDiffer):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_clone_across_pools_to_ro_pool(self):
        snapname = ZFSTest.pool.makeName(b"fs2@snap")
        name = ZFSTest.readonly_pool.makeName(b"fs1/clone1")

        lzc.lzc_snapshot([snapname])

        # it's legal to report either of the conditions
        with self.assertRaises((lzc_exc.ReadOnlyPool, lzc_exc.PoolsDiffer)):
            lzc.lzc_clone(name, snapname)
        self.assertNotExists(name)

    def test_destroy_cloned_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"fs2@origin4")
        snapname2 = ZFSTest.pool.makeName(b"fs1@snap")
        clonename = ZFSTest.pool.makeName(b"fs1/fs/clone4")
        snaps = [snapname1, snapname2]

        lzc.lzc_snapshot(snaps)
        lzc.lzc_clone(clonename, snapname1)

        with self.assertRaises(lzc_exc.SnapshotDestructionFailure) as ctx:
            lzc.lzc_destroy_snaps(snaps, False)

        self.assertEqual(len(ctx.exception.errors), 1)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotIsCloned)
        for snap in snaps:
            self.assertExists(snap)

    def test_deferred_destroy_cloned_fs(self):
        snapname1 = ZFSTest.pool.makeName(b"fs2@origin5")
        snapname2 = ZFSTest.pool.makeName(b"fs1@snap")
        clonename = ZFSTest.pool.makeName(b"fs1/fs/clone5")
        snaps = [snapname1, snapname2]

        lzc.lzc_snapshot(snaps)
        lzc.lzc_clone(clonename, snapname1)

        lzc.lzc_destroy_snaps(snaps, defer=True)

        self.assertExists(snapname1)
        self.assertNotExists(snapname2)

    def test_rollback(self):
        name = ZFSTest.pool.makeName(b"fs1")
        snapname = name + b"@snap"

        lzc.lzc_snapshot([snapname])
        ret = lzc.lzc_rollback(name)
        self.assertEqual(ret, snapname)

    def test_rollback_2(self):
        name = ZFSTest.pool.makeName(b"fs1")
        snapname1 = name + b"@snap1"
        snapname2 = name + b"@snap2"

        lzc.lzc_snapshot([snapname1])
        lzc.lzc_snapshot([snapname2])
        ret = lzc.lzc_rollback(name)
        self.assertEqual(ret, snapname2)

    def test_rollback_no_snaps(self):
        name = ZFSTest.pool.makeName(b"fs1")

        with self.assertRaises(lzc_exc.SnapshotNotFound):
            lzc.lzc_rollback(name)

    def test_rollback_non_existent_fs(self):
        name = ZFSTest.pool.makeName(b"nonexistent")

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_rollback(name)

    def test_rollback_invalid_fs_name(self):
        name = ZFSTest.pool.makeName(b"bad~name")

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_rollback(name)

    def test_rollback_snap_name(self):
        name = ZFSTest.pool.makeName(b"fs1@snap")

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_rollback(name)

    def test_rollback_snap_name_2(self):
        name = ZFSTest.pool.makeName(b"fs1@snap")

        lzc.lzc_snapshot([name])
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_rollback(name)

    def test_rollback_too_long_fs_name(self):
        name = ZFSTest.pool.makeTooLongName()

        with self.assertRaises(lzc_exc.NameTooLong):
            lzc.lzc_rollback(name)

    def test_rollback_to_snap_name(self):
        name = ZFSTest.pool.makeName(b"fs1")
        snap = name + b"@snap"

        lzc.lzc_snapshot([snap])
        lzc.lzc_rollback_to(name, snap)

    def test_rollback_to_not_latest(self):
        fsname = ZFSTest.pool.makeName(b'fs1')
        snap1 = fsname + b"@snap1"
        snap2 = fsname + b"@snap2"

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        with self.assertRaises(lzc_exc.SnapshotNotLatest):
            lzc.lzc_rollback_to(fsname, fsname + b"@snap1")

    @skipUnlessBookmarksSupported
    def test_bookmarks(self):
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs1#bmark1'), ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        lzc.lzc_bookmark(bmark_dict)

    @skipUnlessBookmarksSupported
    def test_bookmarks_2(self):
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs1#bmark1'), ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}
        lzc.lzc_snapshot(snaps)
        lzc.lzc_bookmark(bmark_dict)
        lzc.lzc_destroy_snaps(snaps, defer=False)

    @skipUnlessBookmarksSupported
    def test_bookmark_copying(self):
        snaps = [ZFSTest.pool.makeName(s) for s in [
            b'fs1@snap1', b'fs1@snap2', b'fs2@snap1']]
        bmarks = [ZFSTest.pool.makeName(x) for x in [
            b'fs1#bmark1', b'fs1#bmark2', b'fs2#bmark1']]
        bmarks_copies = [ZFSTest.pool.makeName(x) for x in [
            b'fs1#bmark1_copy', b'fs1#bmark2_copy', b'fs2#bmark1_copy']]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}
        bmark_copies_dict = {x: y for x, y in zip(bmarks_copies, bmarks)}

        for snap in snaps:
            lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict)

        lzc.lzc_bookmark(bmark_copies_dict)
        lzc.lzc_destroy_bookmarks(bmarks_copies)

        lzc.lzc_destroy_bookmarks(bmarks)
        lzc.lzc_destroy_snaps(snaps, defer=False)

    @skipUnlessBookmarksSupported
    def test_bookmarks_empty(self):
        lzc.lzc_bookmark({})

    @skipUnlessBookmarksSupported
    def test_bookmarks_foreign_source(self):
        snaps = [ZFSTest.pool.makeName(b'fs1@snap1')]
        bmarks = [ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.BookmarkMismatch)

    @skipUnlessBookmarksSupported
    def test_bookmarks_invalid_name(self):
        snaps = [ZFSTest.pool.makeName(b'fs1@snap1')]
        bmarks = [ZFSTest.pool.makeName(b'fs1#bmark!')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)

    @skipUnlessBookmarksSupported
    def test_bookmarks_invalid_name_2(self):
        snaps = [ZFSTest.pool.makeName(b'fs1@snap1')]
        bmarks = [ZFSTest.pool.makeName(b'fs1@bmark')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)

    @skipUnlessBookmarksSupported
    def test_bookmarks_too_long_name(self):
        snaps = [ZFSTest.pool.makeName(b'fs1@snap1')]
        bmarks = [ZFSTest.pool.makeTooLongName(b'fs1#')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)

    @skipUnlessBookmarksSupported
    def test_bookmarks_too_long_name_2(self):
        snaps = [ZFSTest.pool.makeName(b'fs1@snap1')]
        bmarks = [ZFSTest.pool.makeTooLongComponent(b'fs1#')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)

    @skipUnlessBookmarksSupported
    def test_bookmarks_foreign_sources(self):
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs2#bmark1'), ZFSTest.pool.makeName(b'fs1#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.BookmarkMismatch)

    @skipUnlessBookmarksSupported
    def test_bookmarks_partially_foreign_sources(self):
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs2#bmark'), ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.BookmarkMismatch)

    @skipUnlessBookmarksSupported
    def test_bookmarks_cross_pool(self):
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.misc_pool.makeName(b'@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs1#bmark1'), ZFSTest.misc_pool.makeName(b'#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps[0:1])
        lzc.lzc_snapshot(snaps[1:2])
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolsDiffer)

    @skipUnlessBookmarksSupported
    def test_bookmarks_missing_snap(self):
        fss = [ZFSTest.pool.makeName(b'fs1'), ZFSTest.pool.makeName(b'fs2')]
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs1#bmark1'), ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        lzc.lzc_snapshot(snaps[0:1])  # only create fs1@snap1

        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotNotFound)

        # no new bookmarks are created if one or more sources do not exist
        for fs in fss:
            fsbmarks = lzc.lzc_get_bookmarks(fs)
            self.assertEqual(len(fsbmarks), 0)

    @skipUnlessBookmarksSupported
    def test_bookmarks_missing_snaps(self):
        fss = [ZFSTest.pool.makeName(b'fs1'), ZFSTest.pool.makeName(b'fs2')]
        snaps = [ZFSTest.pool.makeName(
            b'fs1@snap1'), ZFSTest.pool.makeName(b'fs2@snap1')]
        bmarks = [ZFSTest.pool.makeName(
            b'fs1#bmark1'), ZFSTest.pool.makeName(b'fs2#bmark1')]
        bmark_dict = {x: y for x, y in zip(bmarks, snaps)}

        # do not create any snapshots

        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.SnapshotNotFound)

        # no new bookmarks are created if one or more sources do not exist
        for fs in fss:
            fsbmarks = lzc.lzc_get_bookmarks(fs)
            self.assertEqual(len(fsbmarks), 0)

    @skipUnlessBookmarksSupported
    def test_bookmarks_for_the_same_snap(self):
        snap = ZFSTest.pool.makeName(b'fs1@snap1')
        bmark1 = ZFSTest.pool.makeName(b'fs1#bmark1')
        bmark2 = ZFSTest.pool.makeName(b'fs1#bmark2')
        bmark_dict = {bmark1: snap, bmark2: snap}

        lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict)

    @skipUnlessBookmarksSupported
    def test_bookmarks_for_the_same_snap_2(self):
        snap = ZFSTest.pool.makeName(b'fs1@snap1')
        bmark1 = ZFSTest.pool.makeName(b'fs1#bmark1')
        bmark2 = ZFSTest.pool.makeName(b'fs1#bmark2')
        bmark_dict1 = {bmark1: snap}
        bmark_dict2 = {bmark2: snap}

        lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict1)
        lzc.lzc_bookmark(bmark_dict2)

    @skipUnlessBookmarksSupported
    def test_bookmarks_duplicate_name(self):
        snap1 = ZFSTest.pool.makeName(b'fs1@snap1')
        snap2 = ZFSTest.pool.makeName(b'fs1@snap2')
        bmark = ZFSTest.pool.makeName(b'fs1#bmark')
        bmark_dict1 = {bmark: snap1}
        bmark_dict2 = {bmark: snap2}

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_bookmark(bmark_dict1)
        with self.assertRaises(lzc_exc.BookmarkFailure) as ctx:
            lzc.lzc_bookmark(bmark_dict2)

        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.BookmarkExists)

    @skipUnlessBookmarksSupported
    def test_get_bookmarks(self):
        snap1 = ZFSTest.pool.makeName(b'fs1@snap1')
        snap2 = ZFSTest.pool.makeName(b'fs1@snap2')
        bmark = ZFSTest.pool.makeName(b'fs1#bmark')
        bmark1 = ZFSTest.pool.makeName(b'fs1#bmark1')
        bmark2 = ZFSTest.pool.makeName(b'fs1#bmark2')
        bmark_dict1 = {bmark1: snap1, bmark2: snap2}
        bmark_dict2 = {bmark: snap2}

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_bookmark(bmark_dict1)
        lzc.lzc_bookmark(bmark_dict2)
        lzc.lzc_destroy_snaps([snap1, snap2], defer=False)

        bmarks = lzc.lzc_get_bookmarks(ZFSTest.pool.makeName(b'fs1'))
        self.assertEqual(len(bmarks), 3)
        for b in b'bmark', b'bmark1', b'bmark2':
            self.assertIn(b, bmarks)
            self.assertIsInstance(bmarks[b], dict)
            self.assertEqual(len(bmarks[b]), 0)

        bmarks = lzc.lzc_get_bookmarks(ZFSTest.pool.makeName(b'fs1'),
                                       [b'guid', b'createtxg', b'creation'])
        self.assertEqual(len(bmarks), 3)
        for b in b'bmark', b'bmark1', b'bmark2':
            self.assertIn(b, bmarks)
            self.assertIsInstance(bmarks[b], dict)
            self.assertEqual(len(bmarks[b]), 3)

    @skipUnlessBookmarksSupported
    def test_get_bookmarks_invalid_property(self):
        snap = ZFSTest.pool.makeName(b'fs1@snap')
        bmark = ZFSTest.pool.makeName(b'fs1#bmark')
        bmark_dict = {bmark: snap}

        lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict)

        bmarks = lzc.lzc_get_bookmarks(
            ZFSTest.pool.makeName(b'fs1'), [b'badprop'])
        self.assertEqual(len(bmarks), 1)
        for b in (b'bmark', ):
            self.assertIn(b, bmarks)
            self.assertIsInstance(bmarks[b], dict)
            self.assertEqual(len(bmarks[b]), 0)

    @skipUnlessBookmarksSupported
    def test_get_bookmarks_nonexistent_fs(self):
        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_get_bookmarks(ZFSTest.pool.makeName(b'nonexistent'))

    @skipUnlessBookmarksSupported
    def test_destroy_bookmarks(self):
        snap = ZFSTest.pool.makeName(b'fs1@snap')
        bmark = ZFSTest.pool.makeName(b'fs1#bmark')
        bmark_dict = {bmark: snap}

        lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict)

        lzc.lzc_destroy_bookmarks(
            [bmark, ZFSTest.pool.makeName(b'fs1#nonexistent')])
        bmarks = lzc.lzc_get_bookmarks(ZFSTest.pool.makeName(b'fs1'))
        self.assertEqual(len(bmarks), 0)

    @skipUnlessBookmarksSupported
    def test_destroy_bookmarks_invalid_name(self):
        snap = ZFSTest.pool.makeName(b'fs1@snap')
        bmark = ZFSTest.pool.makeName(b'fs1#bmark')
        bmark_dict = {bmark: snap}

        lzc.lzc_snapshot([snap])
        lzc.lzc_bookmark(bmark_dict)

        with self.assertRaises(lzc_exc.BookmarkDestructionFailure) as ctx:
            lzc.lzc_destroy_bookmarks(
                [bmark, ZFSTest.pool.makeName(b'fs1/nonexistent')])
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)

        bmarks = lzc.lzc_get_bookmarks(ZFSTest.pool.makeName(b'fs1'))
        self.assertEqual(len(bmarks), 1)
        self.assertIn(b'bmark', bmarks)

    @skipUnlessBookmarksSupported
    def test_destroy_bookmark_nonexistent_fs(self):
        lzc.lzc_destroy_bookmarks(
            [ZFSTest.pool.makeName(b'nonexistent#bmark')])

    @skipUnlessBookmarksSupported
    def test_destroy_bookmarks_empty(self):
        lzc.lzc_bookmark({})

    def test_snaprange_space(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        snap3 = ZFSTest.pool.makeName(b"fs1@snap")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_snapshot([snap3])

        space = lzc.lzc_snaprange_space(snap1, snap2)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_snaprange_space(snap2, snap3)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_snaprange_space(snap1, snap3)
        self.assertIsInstance(space, (int, int))

    def test_snaprange_space_2(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        snap3 = ZFSTest.pool.makeName(b"fs1@snap")

        lzc.lzc_snapshot([snap1])
        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap2])
        lzc.lzc_snapshot([snap3])

        space = lzc.lzc_snaprange_space(snap1, snap2)
        self.assertGreater(space, 1024 * 1024)
        space = lzc.lzc_snaprange_space(snap2, snap3)
        self.assertGreater(space, 1024 * 1024)
        space = lzc.lzc_snaprange_space(snap1, snap3)
        self.assertGreater(space, 1024 * 1024)

    def test_snaprange_space_same_snap(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")

        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap])

        space = lzc.lzc_snaprange_space(snap, snap)
        self.assertGreater(space, 1024 * 1024)
        self.assertAlmostEqual(space, 1024 * 1024, delta=1024 * 1024 // 20)

    def test_snaprange_space_wrong_order(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.SnapshotMismatch):
            lzc.lzc_snaprange_space(snap2, snap1)

    def test_snaprange_space_unrelated(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs2@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.SnapshotMismatch):
            lzc.lzc_snaprange_space(snap1, snap2)

    def test_snaprange_space_across_pools(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.misc_pool.makeName(b"@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.PoolsDiffer):
            lzc.lzc_snaprange_space(snap1, snap2)

    def test_snaprange_space_nonexistent(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
            lzc.lzc_snaprange_space(snap1, snap2)
        self.assertEqual(ctx.exception.name, snap2)

        with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
            lzc.lzc_snaprange_space(snap2, snap1)
        self.assertEqual(ctx.exception.name, snap1)

    def test_snaprange_space_invalid_name(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@sn#p")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_snaprange_space(snap1, snap2)

    def test_snaprange_space_not_snap(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_snaprange_space(snap1, snap2)
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_snaprange_space(snap2, snap1)

    def test_snaprange_space_not_snap_2(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1#bmark")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_snaprange_space(snap1, snap2)
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_snaprange_space(snap2, snap1)

    def test_send_space(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        snap3 = ZFSTest.pool.makeName(b"fs1@snap")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_snapshot([snap3])

        space = lzc.lzc_send_space(snap2, snap1)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_send_space(snap3, snap2)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_send_space(snap3, snap1)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_send_space(snap1)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_send_space(snap2)
        self.assertIsInstance(space, (int, int))
        space = lzc.lzc_send_space(snap3)
        self.assertIsInstance(space, (int, int))

    def test_send_space_2(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        snap3 = ZFSTest.pool.makeName(b"fs1@snap")

        lzc.lzc_snapshot([snap1])
        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap2])
        lzc.lzc_snapshot([snap3])

        space = lzc.lzc_send_space(snap2, snap1)
        self.assertGreater(space, 1024 * 1024)

        space = lzc.lzc_send_space(snap3, snap2)

        space = lzc.lzc_send_space(snap3, snap1)

        space_empty = lzc.lzc_send_space(snap1)

        space = lzc.lzc_send_space(snap2)
        self.assertGreater(space, 1024 * 1024)

        space = lzc.lzc_send_space(snap3)
        self.assertEqual(space, space_empty)

    def test_send_space_same_snap(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        lzc.lzc_snapshot([snap1])
        with self.assertRaises(lzc_exc.SnapshotMismatch):
            lzc.lzc_send_space(snap1, snap1)

    def test_send_space_wrong_order(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.SnapshotMismatch):
            lzc.lzc_send_space(snap1, snap2)

    def test_send_space_unrelated(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs2@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.SnapshotMismatch):
            lzc.lzc_send_space(snap1, snap2)

    def test_send_space_across_pools(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.misc_pool.makeName(b"@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with self.assertRaises(lzc_exc.PoolsDiffer):
            lzc.lzc_send_space(snap1, snap2)

    def test_send_space_nonexistent(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs2@snap2")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
            lzc.lzc_send_space(snap1, snap2)
        self.assertEqual(ctx.exception.name, snap1)

        with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
            lzc.lzc_send_space(snap2, snap1)
        self.assertEqual(ctx.exception.name, snap2)

        with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
            lzc.lzc_send_space(snap2)
        self.assertEqual(ctx.exception.name, snap2)

    def test_send_space_invalid_name(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@sn!p")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid) as ctx:
            lzc.lzc_send_space(snap2, snap1)
        self.assertEqual(ctx.exception.name, snap2)
        with self.assertRaises(lzc_exc.NameInvalid) as ctx:
            lzc.lzc_send_space(snap2)
        self.assertEqual(ctx.exception.name, snap2)
        with self.assertRaises(lzc_exc.NameInvalid) as ctx:
            lzc.lzc_send_space(snap1, snap2)
        self.assertEqual(ctx.exception.name, snap2)

    def test_send_space_not_snap(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_send_space(snap1, snap2)
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_send_space(snap2, snap1)
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_send_space(snap2)

    def test_send_space_not_snap_2(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1#bmark")

        lzc.lzc_snapshot([snap1])

        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_send_space(snap2, snap1)
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_send_space(snap2)

    def test_send_full(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")

        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            estimate = lzc.lzc_send_space(snap)

            fd = output.fileno()
            lzc.lzc_send(snap, None, fd)
            st = os.fstat(fd)
            # 5%, arbitrary.
            self.assertAlmostEqual(st.st_size, estimate, delta=estimate // 20)

    def test_send_incremental(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])
        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap2])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            estimate = lzc.lzc_send_space(snap2, snap1)

            fd = output.fileno()
            lzc.lzc_send(snap2, snap1, fd)
            st = os.fstat(fd)
            # 5%, arbitrary.
            self.assertAlmostEqual(st.st_size, estimate, delta=estimate // 20)

    def test_send_flags(self):
        flags = ['embedded_data', 'large_blocks', 'compress', 'raw']
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        for c in range(len(flags)):
            for flag in itertools.permutations(flags, c + 1):
                with dev_null() as fd:
                    lzc.lzc_send(snap, None, fd, list(flag))

    def test_send_unknown_flags(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])
        with dev_null() as fd:
            with self.assertRaises(lzc_exc.UnknownStreamFeature):
                lzc.lzc_send(snap, None, fd, ['embedded_data', 'UNKNOWN'])

    def test_send_same_snap(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        lzc.lzc_snapshot([snap1])
        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.SnapshotMismatch):
                lzc.lzc_send(snap1, snap1, fd)

    def test_send_wrong_order(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.SnapshotMismatch):
                lzc.lzc_send(snap1, snap2, fd)

    def test_send_unrelated(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs2@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.SnapshotMismatch):
                lzc.lzc_send(snap1, snap2, fd)

    def test_send_across_pools(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.misc_pool.makeName(b"@snap2")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.PoolsDiffer):
                lzc.lzc_send(snap1, snap2, fd)

    def test_send_nonexistent(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")

        lzc.lzc_snapshot([snap1])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
                lzc.lzc_send(snap1, snap2, fd)
            self.assertEqual(ctx.exception.name, snap1)

            with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
                lzc.lzc_send(snap2, snap1, fd)
            self.assertEqual(ctx.exception.name, snap2)

            with self.assertRaises(lzc_exc.SnapshotNotFound) as ctx:
                lzc.lzc_send(snap2, None, fd)
            self.assertEqual(ctx.exception.name, snap2)

    def test_send_invalid_name(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@sn!p")

        lzc.lzc_snapshot([snap1])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.NameInvalid) as ctx:
                lzc.lzc_send(snap2, snap1, fd)
            self.assertEqual(ctx.exception.name, snap2)
            with self.assertRaises(lzc_exc.NameInvalid) as ctx:
                lzc.lzc_send(snap2, None, fd)
            self.assertEqual(ctx.exception.name, snap2)
            with self.assertRaises(lzc_exc.NameInvalid) as ctx:
                lzc.lzc_send(snap1, snap2, fd)
            self.assertEqual(ctx.exception.name, snap2)

    # XXX Although undocumented the API allows to create an incremental
    # or full stream for a filesystem as if a temporary unnamed snapshot
    # is taken at some time after the call is made and before the stream
    # starts being produced.
    def test_send_filesystem(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.makeName(b"fs1")

        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            lzc.lzc_send(fs, snap, fd)
            lzc.lzc_send(fs, None, fd)

    def test_send_from_filesystem(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.makeName(b"fs1")

        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.NameInvalid):
                lzc.lzc_send(snap, fs, fd)

    @skipUnlessBookmarksSupported
    def test_send_bookmark(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        bmark = ZFSTest.pool.makeName(b"fs1#bmark")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_bookmark({bmark: snap2})
        lzc.lzc_destroy_snaps([snap2], defer=False)

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            with self.assertRaises(lzc_exc.NameInvalid):
                lzc.lzc_send(bmark, snap1, fd)
            with self.assertRaises(lzc_exc.NameInvalid):
                lzc.lzc_send(bmark, None, fd)

    @skipUnlessBookmarksSupported
    def test_send_from_bookmark(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        bmark = ZFSTest.pool.makeName(b"fs1#bmark")

        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])
        lzc.lzc_bookmark({bmark: snap1})
        lzc.lzc_destroy_snaps([snap1], defer=False)

        with tempfile.TemporaryFile(suffix='.zstream') as output:
            fd = output.fileno()
            lzc.lzc_send(snap2, bmark, fd)

    def test_send_bad_fd(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile() as tmp:
            bad_fd = tmp.fileno()

        with self.assertRaises(lzc_exc.StreamIOError) as ctx:
            lzc.lzc_send(snap, None, bad_fd)
        self.assertEqual(ctx.exception.errno, errno.EBADF)

    def test_send_bad_fd_2(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        with self.assertRaises(lzc_exc.StreamIOError) as ctx:
            lzc.lzc_send(snap, None, -2)
        self.assertEqual(ctx.exception.errno, errno.EBADF)

    def test_send_bad_fd_3(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile() as tmp:
            bad_fd = tmp.fileno()

        (soft, hard) = resource.getrlimit(resource.RLIMIT_NOFILE)
        bad_fd = hard + 1
        with self.assertRaises(lzc_exc.StreamIOError) as ctx:
            lzc.lzc_send(snap, None, bad_fd)
        self.assertEqual(ctx.exception.errno, errno.EBADF)

    def test_send_to_broken_pipe(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        if sys.version_info < (3, 0):
            proc = subprocess.Popen(['true'], stdin=subprocess.PIPE)
            proc.wait()
            with self.assertRaises(lzc_exc.StreamIOError) as ctx:
                lzc.lzc_send(snap, None, proc.stdin.fileno())
            self.assertEqual(ctx.exception.errno, errno.EPIPE)
        else:
            with subprocess.Popen(['true'], stdin=subprocess.PIPE) as proc:
                proc.wait()
                with self.assertRaises(lzc_exc.StreamIOError) as ctx:
                    lzc.lzc_send(snap, None, proc.stdin.fileno())
                self.assertEqual(ctx.exception.errno, errno.EPIPE)

    def test_send_to_broken_pipe_2(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                for i in range(1024):
                    f.write(b'x' * 1024)
                f.flush()
                lzc.lzc_snapshot([snap])

        if sys.version_info < (3, 0):
            p = subprocess.Popen(['sleep', '2'], stdin=subprocess.PIPE)
            with self.assertRaises(lzc_exc.StreamIOError) as ctx:
                lzc.lzc_send(snap, None, p.stdin.fileno())
            self.assertTrue(ctx.exception.errno == errno.EPIPE or
                            ctx.exception.errno == errno.EINTR)
        else:
            with subprocess.Popen(['sleep', '2'], stdin=subprocess.PIPE) as p:
                with self.assertRaises(lzc_exc.StreamIOError) as ctx:
                    lzc.lzc_send(snap, None, p.stdin.fileno())
                self.assertTrue(ctx.exception.errno == errno.EPIPE or
                                ctx.exception.errno == errno.EINTR)

    def test_send_to_ro_file(self):
        snap = ZFSTest.pool.makeName(b"fs1@snap")
        lzc.lzc_snapshot([snap])

        with tempfile.NamedTemporaryFile(
                suffix='.zstream', delete=False) as output:
            # tempfile always opens a temporary file in read-write mode
            # regardless of the specified mode, so we have to open it again.
            os.chmod(output.name, stat.S_IRUSR)
            fd = os.open(output.name, os.O_RDONLY)
            with self.assertRaises(lzc_exc.StreamIOError) as ctx:
                lzc.lzc_send(snap, None, fd)
            os.close(fd)
            os.unlink(output.name)

        self.assertEqual(ctx.exception.errno, errno.EBADF)

    def test_recv_full(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dst = ZFSTest.pool.makeName(b"fs2/received-1@snap")

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")) as name:
            lzc.lzc_snapshot([src])

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst, stream.fileno())

        name = os.path.basename(name)
        with zfs_mount(src) as mnt1, zfs_mount(dst) as mnt2:
            self.assertTrue(
                filecmp.cmp(
                    os.path.join(mnt1, name), os.path.join(mnt2, name), False))

    def test_recv_incremental(self):
        src1 = ZFSTest.pool.makeName(b"fs1@snap1")
        src2 = ZFSTest.pool.makeName(b"fs1@snap2")
        dst1 = ZFSTest.pool.makeName(b"fs2/received-2@snap1")
        dst2 = ZFSTest.pool.makeName(b"fs2/received-2@snap2")

        lzc.lzc_snapshot([src1])
        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")) as name:
            lzc.lzc_snapshot([src2])

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src1, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst1, stream.fileno())
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src2, src1, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst2, stream.fileno())

        name = os.path.basename(name)
        with zfs_mount(src2) as mnt1, zfs_mount(dst2) as mnt2:
            self.assertTrue(
                filecmp.cmp(
                    os.path.join(mnt1, name), os.path.join(mnt2, name), False))

    # This test case fails unless a patch from
    # https://clusterhq.atlassian.net/browse/ZFS-20
    # is applied to libzfs_core, otherwise it succeeds.
    @unittest.skip("fails with unpatched libzfs_core")
    def test_recv_without_explicit_snap_name(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-100")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dstfs, full.fileno())
            lzc.lzc_receive(dstfs, incr.fileno())
        self.assertExists(dst1)
        self.assertExists(dst2)

    def test_recv_clone(self):
        orig_src = ZFSTest.pool.makeName(b"fs2@send-origin")
        clone = ZFSTest.pool.makeName(b"fs1/fs/send-clone")
        clone_snap = clone + b"@snap"
        orig_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-origin@snap")
        clone_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-clone@snap")

        lzc.lzc_snapshot([orig_src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(orig_src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(orig_dst, stream.fileno())

        lzc.lzc_clone(clone, orig_src)
        lzc.lzc_snapshot([clone_snap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(clone_snap, orig_src, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(clone_dst, stream.fileno(), origin=orig_dst)

    def test_recv_full_already_existing_empty_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-3")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        lzc.lzc_create(dstfs)
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises((
                    lzc_exc.DestinationModified, lzc_exc.DatasetExists)):
                lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_into_root_empty_pool(self):
        empty_pool = None
        try:
            srcfs = ZFSTest.pool.makeName(b"fs1")
            empty_pool = _TempPool()
            dst = empty_pool.makeName(b'@snap')

            with streams(srcfs, b"snap", None) as (_, (stream, _)):
                with self.assertRaises((
                        lzc_exc.DestinationModified, lzc_exc.DatasetExists)):
                    lzc.lzc_receive(dst, stream.fileno())
        finally:
            if empty_pool is not None:
                empty_pool.cleanUp()

    def test_recv_full_into_ro_pool(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        dst = ZFSTest.readonly_pool.makeName(b'fs2/received@snap')

        with streams(srcfs, b"snap", None) as (_, (stream, _)):
            with self.assertRaises(lzc_exc.ReadOnlyPool):
                lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_already_existing_modified_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-5")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        lzc.lzc_create(dstfs)
        with temp_file_in_fs(dstfs):
            with tempfile.TemporaryFile(suffix='.zstream') as stream:
                lzc.lzc_send(src, None, stream.fileno())
                stream.seek(0)
                with self.assertRaises((
                        lzc_exc.DestinationModified, lzc_exc.DatasetExists)):
                    lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_already_existing_with_snapshots(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-4")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        lzc.lzc_create(dstfs)
        lzc.lzc_snapshot([dstfs + b"@snap1"])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises((
                    lzc_exc.StreamMismatch, lzc_exc.DatasetExists)):
                lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_already_existing_snapshot(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-6")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        lzc.lzc_create(dstfs)
        lzc.lzc_snapshot([dst])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.DatasetExists):
                lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_missing_parent_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dst = ZFSTest.pool.makeName(b"fs2/nonexistent/fs@snap")

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.DatasetNotFound):
                lzc.lzc_receive(dst, stream.fileno())

    def test_recv_full_but_specify_origin(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src = srcfs + b"@snap"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-30")
        dst = dstfs + b'@snap'
        origin1 = ZFSTest.pool.makeName(b"fs2@snap1")
        origin2 = ZFSTest.pool.makeName(b"fs2@snap2")

        lzc.lzc_snapshot([origin1])
        with streams(srcfs, src, None) as (_, (stream, _)):
            lzc.lzc_receive(dst, stream.fileno(), origin=origin1)
            origin = ZFSTest.pool.getFilesystem(
                b"fs2/received-30").getProperty('origin')
            self.assertEqual(origin, origin1)
            stream.seek(0)
            # because origin snap does not exist can't receive as a clone of it
            with self.assertRaises((
                    lzc_exc.DatasetNotFound,
                    lzc_exc.BadStream)):
                lzc.lzc_receive(dst, stream.fileno(), origin=origin2)

    def test_recv_full_existing_empty_fs_and_origin(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src = srcfs + b"@snap"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-31")
        dst = dstfs + b'@snap'
        origin = dstfs + b'@dummy'

        lzc.lzc_create(dstfs)
        with streams(srcfs, src, None) as (_, (stream, _)):
            # because the destination fs already exists and has no snaps
            with self.assertRaises((
                    lzc_exc.DestinationModified,
                    lzc_exc.DatasetExists,
                    lzc_exc.BadStream)):
                lzc.lzc_receive(dst, stream.fileno(), origin=origin)
            lzc.lzc_snapshot([origin])
            stream.seek(0)
            # because the destination fs already exists and has the snap
            with self.assertRaises((
                    lzc_exc.StreamMismatch,
                    lzc_exc.DatasetExists,
                    lzc_exc.BadStream)):
                lzc.lzc_receive(dst, stream.fileno(), origin=origin)

    def test_recv_incremental_mounted_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-7")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with zfs_mount(dstfs):
                lzc.lzc_receive(dst2, incr.fileno())

    def test_recv_incremental_modified_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-15")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                with self.assertRaises(lzc_exc.DestinationModified):
                    lzc.lzc_receive(dst2, incr.fileno())

    def test_recv_incremental_snapname_used(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-8")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_snapshot([dst2])
            with self.assertRaises(lzc_exc.DatasetExists):
                lzc.lzc_receive(dst2, incr.fileno())

    def test_recv_incremental_more_recent_snap_with_no_changes(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-9")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_snapshot([dst_snap])
            lzc.lzc_receive(dst2, incr.fileno())

    def test_recv_incremental_non_clone_but_set_origin(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-20")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_snapshot([dst_snap])
            # because cannot receive incremental and set origin on a non-clone
            with self.assertRaises(lzc_exc.BadStream):
                lzc.lzc_receive(dst2, incr.fileno(), origin=dst1)

    def test_recv_incremental_non_clone_but_set_random_origin(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-21")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_snapshot([dst_snap])
            # because origin snap does not exist can't receive as a clone of it
            with self.assertRaises((
                    lzc_exc.DatasetNotFound,
                    lzc_exc.BadStream)):
                lzc.lzc_receive(
                    dst2, incr.fileno(),
                    origin=ZFSTest.pool.makeName(b"fs2/fs@snap"))

    def test_recv_incremental_more_recent_snap(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-10")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                lzc.lzc_snapshot([dst_snap])
                with self.assertRaises(lzc_exc.DestinationModified):
                    lzc.lzc_receive(dst2, incr.fileno())

    def test_recv_incremental_duplicate(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-11")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_receive(dst2, incr.fileno())
            incr.seek(0)
            with self.assertRaises(lzc_exc.DestinationModified):
                lzc.lzc_receive(dst_snap, incr.fileno())

    def test_recv_incremental_unrelated_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-12")
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (_, incr)):
            lzc.lzc_create(dstfs)
            with self.assertRaises(lzc_exc.StreamMismatch):
                lzc.lzc_receive(dst_snap, incr.fileno())

    def test_recv_incremental_nonexistent_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-13")
        dst_snap = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (_, incr)):
            with self.assertRaises(lzc_exc.DatasetNotFound):
                lzc.lzc_receive(dst_snap, incr.fileno())

    def test_recv_incremental_same_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        src_snap = srcfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (_, incr)):
            with self.assertRaises(lzc_exc.DestinationModified):
                lzc.lzc_receive(src_snap, incr.fileno())

    def test_recv_clone_without_specifying_origin(self):
        orig_src = ZFSTest.pool.makeName(b"fs2@send-origin-2")
        clone = ZFSTest.pool.makeName(b"fs1/fs/send-clone-2")
        clone_snap = clone + b"@snap"
        orig_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-origin-2@snap")
        clone_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-clone-2@snap")

        lzc.lzc_snapshot([orig_src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(orig_src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(orig_dst, stream.fileno())

        lzc.lzc_clone(clone, orig_src)
        lzc.lzc_snapshot([clone_snap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(clone_snap, orig_src, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.BadStream):
                lzc.lzc_receive(clone_dst, stream.fileno())

    def test_recv_clone_invalid_origin(self):
        orig_src = ZFSTest.pool.makeName(b"fs2@send-origin-3")
        clone = ZFSTest.pool.makeName(b"fs1/fs/send-clone-3")
        clone_snap = clone + b"@snap"
        orig_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-origin-3@snap")
        clone_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-clone-3@snap")

        lzc.lzc_snapshot([orig_src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(orig_src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(orig_dst, stream.fileno())

        lzc.lzc_clone(clone, orig_src)
        lzc.lzc_snapshot([clone_snap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(clone_snap, orig_src, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.NameInvalid):
                lzc.lzc_receive(
                    clone_dst, stream.fileno(),
                    origin=ZFSTest.pool.makeName(b"fs1/fs"))

    def test_recv_clone_wrong_origin(self):
        orig_src = ZFSTest.pool.makeName(b"fs2@send-origin-4")
        clone = ZFSTest.pool.makeName(b"fs1/fs/send-clone-4")
        clone_snap = clone + b"@snap"
        orig_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-origin-4@snap")
        clone_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-clone-4@snap")
        wrong_origin = ZFSTest.pool.makeName(b"fs1/fs@snap")

        lzc.lzc_snapshot([orig_src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(orig_src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(orig_dst, stream.fileno())

        lzc.lzc_clone(clone, orig_src)
        lzc.lzc_snapshot([clone_snap])
        lzc.lzc_snapshot([wrong_origin])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(clone_snap, orig_src, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.StreamMismatch):
                lzc.lzc_receive(
                    clone_dst, stream.fileno(), origin=wrong_origin)

    def test_recv_clone_nonexistent_origin(self):
        orig_src = ZFSTest.pool.makeName(b"fs2@send-origin-5")
        clone = ZFSTest.pool.makeName(b"fs1/fs/send-clone-5")
        clone_snap = clone + b"@snap"
        orig_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-origin-5@snap")
        clone_dst = ZFSTest.pool.makeName(b"fs1/fs/recv-clone-5@snap")
        wrong_origin = ZFSTest.pool.makeName(b"fs1/fs@snap")

        lzc.lzc_snapshot([orig_src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(orig_src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(orig_dst, stream.fileno())

        lzc.lzc_clone(clone, orig_src)
        lzc.lzc_snapshot([clone_snap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(clone_snap, orig_src, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.DatasetNotFound):
                lzc.lzc_receive(
                    clone_dst, stream.fileno(), origin=wrong_origin)

    def test_force_recv_full_existing_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-50")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])

        lzc.lzc_create(dstfs)
        with temp_file_in_fs(dstfs):
            pass  # enough to taint the fs

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst, stream.fileno(), force=True)

    def test_force_recv_full_existing_modified_mounted_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-53")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])

        lzc.lzc_create(dstfs)

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with zfs_mount(dstfs) as mntdir:
                f = tempfile.NamedTemporaryFile(dir=mntdir, delete=False)
                for i in range(1024):
                    f.write(b'x' * 1024)
                lzc.lzc_receive(dst, stream.fileno(), force=True)
                # The temporary file disappears and any access, even close(),
                # results in EIO.
                self.assertFalse(os.path.exists(f.name))
                with self.assertRaises(IOError):
                    f.close()

    # This test-case expects the behavior that should be there,
    # at the moment it may fail with DatasetExists or StreamMismatch
    # depending on the implementation.
    def test_force_recv_full_already_existing_with_snapshots(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-51")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])

        lzc.lzc_create(dstfs)
        with temp_file_in_fs(dstfs):
            pass  # enough to taint the fs
        lzc.lzc_snapshot([dstfs + b"@snap1"])

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst, stream.fileno(), force=True)

    def test_force_recv_full_already_existing_with_same_snap(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.makeName(b"fs2/received-52")
        dst = dstfs + b'@snap'

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])

        lzc.lzc_create(dstfs)
        with temp_file_in_fs(dstfs):
            pass  # enough to taint the fs
        lzc.lzc_snapshot([dst])

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.DatasetExists):
                lzc.lzc_receive(dst, stream.fileno(), force=True)

    def test_force_recv_full_missing_parent_fs(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dst = ZFSTest.pool.makeName(b"fs2/nonexistent/fs@snap")

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")):
            lzc.lzc_snapshot([src])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.DatasetNotFound):
                lzc.lzc_receive(dst, stream.fileno(), force=True)

    def test_force_recv_incremental_modified_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-60")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                pass  # enough to taint the fs
            lzc.lzc_receive(dst2, incr.fileno(), force=True)

    def test_force_recv_incremental_modified_mounted_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-64")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with zfs_mount(dstfs) as mntdir:
                f = tempfile.NamedTemporaryFile(dir=mntdir, delete=False)
                for i in range(1024):
                    f.write(b'x' * 1024)
                lzc.lzc_receive(dst2, incr.fileno(), force=True)
                # The temporary file disappears and any access, even close(),
                # results in EIO.
                self.assertFalse(os.path.exists(f.name))
                with self.assertRaises(IOError):
                    f.close()

    def test_force_recv_incremental_modified_fs_plus_later_snap(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-61")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst3 = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                pass  # enough to taint the fs
            lzc.lzc_snapshot([dst3])
            lzc.lzc_receive(dst2, incr.fileno(), force=True)
        self.assertExists(dst1)
        self.assertExists(dst2)
        self.assertNotExists(dst3)

    def test_force_recv_incremental_modified_fs_plus_same_name_snap(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-62")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                pass  # enough to taint the fs
            lzc.lzc_snapshot([dst2])
            with self.assertRaises(lzc_exc.DatasetExists):
                lzc.lzc_receive(dst2, incr.fileno(), force=True)

    def test_force_recv_incremental_modified_fs_plus_held_snap(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-63")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst3 = dstfs + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                pass  # enough to taint the fs
            lzc.lzc_snapshot([dst3])
            with cleanup_fd() as cfd:
                lzc.lzc_hold({dst3: b'tag'}, cfd)
                with self.assertRaises(lzc_exc.DatasetBusy):
                    lzc.lzc_receive(dst2, incr.fileno(), force=True)
        self.assertExists(dst1)
        self.assertNotExists(dst2)
        self.assertExists(dst3)

    def test_force_recv_incremental_modified_fs_plus_cloned_snap(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-70")
        dst1 = dstfs + b'@snap1'
        dst2 = dstfs + b'@snap2'
        dst3 = dstfs + b'@snap'
        cloned = ZFSTest.pool.makeName(b"fs2/received-cloned-70")

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            with temp_file_in_fs(dstfs):
                pass  # enough to taint the fs
            lzc.lzc_snapshot([dst3])
            lzc.lzc_clone(cloned, dst3)
            with self.assertRaises(lzc_exc.DatasetExists):
                lzc.lzc_receive(dst2, incr.fileno(), force=True)
        self.assertExists(dst1)
        self.assertNotExists(dst2)
        self.assertExists(dst3)

    def test_recv_incremental_into_cloned_fs(self):
        srcfs = ZFSTest.pool.makeName(b"fs1")
        src1 = srcfs + b"@snap1"
        src2 = srcfs + b"@snap2"
        dstfs = ZFSTest.pool.makeName(b"fs2/received-71")
        dst1 = dstfs + b'@snap1'
        cloned = ZFSTest.pool.makeName(b"fs2/received-cloned-71")
        dst2 = cloned + b'@snap'

        with streams(srcfs, src1, src2) as (_, (full, incr)):
            lzc.lzc_receive(dst1, full.fileno())
            lzc.lzc_clone(cloned, dst1)
            # test both graceful and with-force attempts
            with self.assertRaises(lzc_exc.StreamMismatch):
                lzc.lzc_receive(dst2, incr.fileno())
            incr.seek(0)
            with self.assertRaises(lzc_exc.StreamMismatch):
                lzc.lzc_receive(dst2, incr.fileno(), force=True)
        self.assertExists(dst1)
        self.assertNotExists(dst2)

    def test_recv_with_header_full(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dst = ZFSTest.pool.makeName(b"fs2/received")

        with temp_file_in_fs(ZFSTest.pool.makeName(b"fs1")) as name:
            lzc.lzc_snapshot([src])

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)

            (header, c_header) = lzc.receive_header(stream.fileno())
            self.assertEqual(src, header['drr_toname'])
            snap = header['drr_toname'].split(b'@', 1)[1]
            lzc.lzc_receive_with_header(
                dst + b'@' + snap, stream.fileno(), c_header)

        name = os.path.basename(name)
        with zfs_mount(src) as mnt1, zfs_mount(dst) as mnt2:
            self.assertTrue(
                filecmp.cmp(
                    os.path.join(mnt1, name), os.path.join(mnt2, name), False))

    def test_recv_fs_below_zvol(self):
        send = ZFSTest.pool.makeName(b"fs1@snap")
        zvol = ZFSTest.pool.makeName(b"fs1/zvol")
        dest = zvol + b"/fs@snap"
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_snapshot([send])
        lzc.lzc_create(zvol, ds_type='zvol', props=props)
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(send, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.WrongParent):
                lzc.lzc_receive(dest, stream.fileno())

    def test_recv_zvol_over_fs_with_children(self):
        parent = ZFSTest.pool.makeName(b"fs1")
        child = parent + b"subfs"
        zvol = ZFSTest.pool.makeName(b"fs1/zvol")
        send = zvol + b"@snap"
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(child)
        lzc.lzc_create(zvol, ds_type='zvol', props=props)
        lzc.lzc_snapshot([send])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(send, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.WrongParent):
                lzc.lzc_receive(parent + b"@snap", stream.fileno(), force=True)

    def test_recv_zvol_overwrite_rootds(self):
        zvol = ZFSTest.pool.makeName(b"fs1/zvol")
        snap = zvol + b"@snap"
        rootds = ZFSTest.pool.getRoot().getName()
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(zvol, ds_type='zvol', props=props)
        lzc.lzc_snapshot([snap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(snap, None, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.WrongParent):
                lzc.lzc_receive(rootds + b"@snap", stream.fileno(), force=True)

    def test_send_full_across_clone_branch_point(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-20", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-20")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, None, stream.fileno())

    def test_send_incr_across_clone_branch_point(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-21", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-21")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, fromsnap, stream.fileno())

    def test_send_resume_token_full(self):
        src = ZFSTest.pool.makeName(b"fs1@snap")
        dstfs = ZFSTest.pool.getFilesystem(b"fs2/received")
        dst = dstfs.getSnap()

        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            for i in range(1, 10):
                with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                    f.write(b'x' * 1024 * i)
                    f.flush()
        lzc.lzc_snapshot([src])

        with tempfile.NamedTemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(src, None, stream.fileno())
            stream.seek(0)
            stream.truncate(1024 * 3)
            with self.assertRaises(lzc_exc.StreamTruncated):
                lzc.lzc_receive_resumable(dst, stream.fileno())
            # Resume token code from zfs_send_resume_token_to_nvlist()
            # XXX: if used more than twice move this code into an external func
            # format: <version>-<cksum>-<packed-size>-<compressed-payload>
            token = dstfs.getProperty("receive_resume_token")
            self.assertNotEqual(token, b'-')
            tokens = token.split(b'-')
            self.assertEqual(len(tokens), 4)
            version = tokens[0]
            packed_size = int(tokens[2], 16)
            compressed_nvs = tokens[3]
            # Validate resume token
            self.assertEqual(version, b'1')  # ZFS_SEND_RESUME_TOKEN_VERSION
            if sys.version_info < (3, 0):
                payload = (
                    zlib.decompress(str(bytearray.fromhex(compressed_nvs)))
                )
            else:
                payload = (
                    zlib.decompress(bytearray.fromhex(compressed_nvs.decode()))
                )
            self.assertEqual(len(payload), packed_size)
            # Unpack
            resume_values = packed_nvlist_out(payload, packed_size)
            resumeobj = resume_values.get(b'object')
            resumeoff = resume_values.get(b'offset')
            with tempfile.NamedTemporaryFile(suffix='.zstream') as rstream:
                lzc.lzc_send_resume(
                    src, None, rstream.fileno(), None, resumeobj, resumeoff)
                rstream.seek(0)
                lzc.lzc_receive_resumable(dst, rstream.fileno())

    def test_send_resume_token_incremental(self):
        snap1 = ZFSTest.pool.makeName(b"fs1@snap1")
        snap2 = ZFSTest.pool.makeName(b"fs1@snap2")
        dstfs = ZFSTest.pool.getFilesystem(b"fs2/received")
        dst1 = dstfs.getSnap()
        dst2 = dstfs.getSnap()

        lzc.lzc_snapshot([snap1])
        with tempfile.NamedTemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(snap1, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(dst1, stream.fileno())

        with zfs_mount(ZFSTest.pool.makeName(b"fs1")) as mntdir:
            for i in range(1, 10):
                with tempfile.NamedTemporaryFile(dir=mntdir) as f:
                    f.write(b'x' * 1024 * i)
                    f.flush()
        lzc.lzc_snapshot([snap2])

        with tempfile.NamedTemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(snap2, snap1, stream.fileno())
            stream.seek(0)
            stream.truncate(1024 * 3)
            with self.assertRaises(lzc_exc.StreamTruncated):
                lzc.lzc_receive_resumable(dst2, stream.fileno())
            # Resume token code from zfs_send_resume_token_to_nvlist()
            # format: <version>-<cksum>-<packed-size>-<compressed-payload>
            token = dstfs.getProperty("receive_resume_token")
            self.assertNotEqual(token, '-')
            tokens = token.split(b'-')
            self.assertEqual(len(tokens), 4)
            version = tokens[0]
            packed_size = int(tokens[2], 16)
            compressed_nvs = tokens[3]
            # Validate resume token
            self.assertEqual(version, b'1')  # ZFS_SEND_RESUME_TOKEN_VERSION
            if sys.version_info < (3, 0):
                payload = (
                     zlib.decompress(str(bytearray.fromhex(compressed_nvs)))
                )
            else:
                payload = (
                    zlib.decompress(bytearray.fromhex(compressed_nvs.decode()))
                )
            self.assertEqual(len(payload), packed_size)
            # Unpack
            resume_values = packed_nvlist_out(payload, packed_size)
            resumeobj = resume_values.get(b'object')
            resumeoff = resume_values.get(b'offset')
            with tempfile.NamedTemporaryFile(suffix='.zstream') as rstream:
                lzc.lzc_send_resume(
                    snap2, snap1, rstream.fileno(), None, resumeobj, resumeoff)
                rstream.seek(0)
                lzc.lzc_receive_resumable(dst2, rstream.fileno())

    def test_recv_full_across_clone_branch_point(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-30", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-30")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        recvfs = ZFSTest.pool.makeName(b"fs1/recv-clone-30")
        recvsnap = recvfs + b"@snap"
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(recvsnap, stream.fileno())

    def test_recv_one(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        tosnap = ZFSTest.pool.makeName(b"recv@snap1")

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            lzc.lzc_receive_one(tosnap, stream.fileno(), c_header)

    def test_recv_one_size(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        tosnap = ZFSTest.pool.makeName(b"recv@snap1")

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            size = os.fstat(stream.fileno()).st_size
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            (read, _) = lzc.lzc_receive_one(tosnap, stream.fileno(), c_header)
            self.assertAlmostEqual(read, size, delta=read * 0.05)

    def test_recv_one_props(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.getFilesystem(b"recv")
        tosnap = fs.getName() + b"@snap1"
        props = {
            b"compression": 0x01,
            b"ns:prop": b"val"
        }

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            lzc.lzc_receive_one(tosnap, stream.fileno(), c_header, props=props)
            self.assertExists(tosnap)
            self.assertEqual(fs.getProperty("compression", "received"), b"on")
            self.assertEqual(fs.getProperty("ns:prop", "received"), b"val")

    def test_recv_one_invalid_prop(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.getFilesystem(b"recv")
        tosnap = fs.getName() + b"@snap1"
        props = {
            b"exec": 0xff,
            b"atime": 0x00
        }

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            with self.assertRaises(lzc_exc.ReceivePropertyFailure) as ctx:
                lzc.lzc_receive_one(
                    tosnap, stream.fileno(), c_header, props=props)
            self.assertExists(tosnap)
            self.assertEqual(fs.getProperty("atime", "received"), b"off")
            for e in ctx.exception.errors:
                self.assertIsInstance(e, lzc_exc.PropertyInvalid)
                self.assertEqual(e.name, b"exec")

    def test_recv_with_cmdprops(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.getFilesystem(b"recv")
        tosnap = fs.getName() + b"@snap1"
        props = {}
        cmdprops = {
            b"compression": 0x01,
            b"ns:prop": b"val"
        }

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            lzc.lzc_receive_with_cmdprops(
                tosnap, stream.fileno(), c_header, props=props,
                cmdprops=cmdprops)
            self.assertExists(tosnap)
            self.assertEqual(fs.getProperty("compression"), b"on")
            self.assertEqual(fs.getProperty("ns:prop"), b"val")

    def test_recv_with_cmdprops_and_recvprops(self):
        fromsnap = ZFSTest.pool.makeName(b"fs1@snap1")
        fs = ZFSTest.pool.getFilesystem(b"recv")
        tosnap = fs.getName() + b"@snap1"
        props = {
            b"atime": 0x01,
            b"exec": 0x00,
            b"ns:prop": b"abc"
        }
        cmdprops = {
            b"compression": 0x01,
            b"ns:prop": b"def",
            b"exec": None,
        }

        lzc.lzc_snapshot([fromsnap])
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            (header, c_header) = lzc.receive_header(stream.fileno())
            lzc.lzc_receive_with_cmdprops(
                tosnap, stream.fileno(), c_header, props=props,
                cmdprops=cmdprops)
            self.assertExists(tosnap)
            self.assertEqual(fs.getProperty("atime", True), b"on")
            self.assertEqual(fs.getProperty("exec", True), b"off")
            self.assertEqual(fs.getProperty("ns:prop", True), b"abc")
            self.assertEqual(fs.getProperty("compression"), b"on")
            self.assertEqual(fs.getProperty("ns:prop"), b"def")
            self.assertEqual(fs.getProperty("exec"), b"on")

    def test_recv_incr_across_clone_branch_point_no_origin(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-32", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-32")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        recvfs = ZFSTest.pool.makeName(b"fs1/recv-clone-32")
        recvsnap1 = recvfs + b"@snap1"
        recvsnap2 = recvfs + b"@snap2"
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(recvsnap1, stream.fileno())
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, fromsnap, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.BadStream):
                lzc.lzc_receive(recvsnap2, stream.fileno())

    def test_recv_incr_across_clone_branch_point(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-31", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-31")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        recvfs = ZFSTest.pool.makeName(b"fs1/recv-clone-31")
        recvsnap1 = recvfs + b"@snap1"
        recvsnap2 = recvfs + b"@snap2"
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(recvsnap1, stream.fileno())
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, fromsnap, stream.fileno())
            stream.seek(0)
            with self.assertRaises(lzc_exc.BadStream):
                lzc.lzc_receive(recvsnap2, stream.fileno(), origin=recvsnap1)

    def test_recv_incr_across_clone_branch_point_new_fs(self):
        origfs = ZFSTest.pool.makeName(b"fs2")

        (_, (fromsnap, origsnap, _)) = make_snapshots(
            origfs, b"snap1", b"send-origin-33", None)

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/send-clone-33")
        lzc.lzc_clone(clonefs, origsnap)

        (_, (_, tosnap, _)) = make_snapshots(clonefs, None, b"snap", None)

        recvfs1 = ZFSTest.pool.makeName(b"fs1/recv-clone-33")
        recvsnap1 = recvfs1 + b"@snap"
        recvfs2 = ZFSTest.pool.makeName(b"fs1/recv-clone-33_2")
        recvsnap2 = recvfs2 + b"@snap"
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(fromsnap, None, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(recvsnap1, stream.fileno())
        with tempfile.TemporaryFile(suffix='.zstream') as stream:
            lzc.lzc_send(tosnap, fromsnap, stream.fileno())
            stream.seek(0)
            lzc.lzc_receive(recvsnap2, stream.fileno(), origin=recvsnap1)

    def test_recv_bad_stream(self):
        dstfs = ZFSTest.pool.makeName(b"fs2/received")
        dst_snap = dstfs + b'@snap'

        with dev_zero() as fd:
            with self.assertRaises(lzc_exc.BadStream):
                lzc.lzc_receive(dst_snap, fd)

    @needs_support(lzc.lzc_promote)
    def test_promote(self):
        origfs = ZFSTest.pool.makeName(b"fs2")
        snap = b"@promote-snap-1"
        origsnap = origfs + snap
        lzc.lzc_snap([origsnap])

        clonefs = ZFSTest.pool.makeName(b"fs1/fs/promote-clone-1")
        lzc.lzc_clone(clonefs, origsnap)

        lzc.lzc_promote(clonefs)
        # the snapshot now should belong to the promoted fs
        self.assertExists(clonefs + snap)

    @needs_support(lzc.lzc_promote)
    def test_promote_too_long_snapname(self):
        # origfs name must be shorter than clonefs name
        origfs = ZFSTest.pool.makeName(b"fs2")
        clonefs = ZFSTest.pool.makeName(b"fs1/fs/promote-clone-2")
        snapprefix = b"@promote-snap-2-"
        pad_len = 1 + lzc.MAXNAMELEN - len(clonefs) - len(snapprefix)
        snap = snapprefix + b'x' * pad_len
        origsnap = origfs + snap

        lzc.lzc_snap([origsnap])
        lzc.lzc_clone(clonefs, origsnap)

        # This may fail on older buggy systems.
        # See: https://www.illumos.org/issues/5909
        with self.assertRaises(lzc_exc.NameTooLong):
            lzc.lzc_promote(clonefs)

    @needs_support(lzc.lzc_promote)
    def test_promote_not_cloned(self):
        fs = ZFSTest.pool.makeName(b"fs2")
        with self.assertRaises(lzc_exc.NotClone):
            lzc.lzc_promote(fs)

    @unittest.skipIf(*illumos_bug_6379())
    def test_hold_bad_fd(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile() as tmp:
            bad_fd = tmp.fileno()

        with self.assertRaises(lzc_exc.BadHoldCleanupFD):
            lzc.lzc_hold({snap: b'tag'}, bad_fd)

    @unittest.skipIf(*illumos_bug_6379())
    def test_hold_bad_fd_2(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with self.assertRaises(lzc_exc.BadHoldCleanupFD):
            lzc.lzc_hold({snap: b'tag'}, -2)

    @unittest.skipIf(*illumos_bug_6379())
    def test_hold_bad_fd_3(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        (soft, hard) = resource.getrlimit(resource.RLIMIT_NOFILE)
        bad_fd = hard + 1
        with self.assertRaises(lzc_exc.BadHoldCleanupFD):
            lzc.lzc_hold({snap: b'tag'}, bad_fd)

    @unittest.skipIf(*illumos_bug_6379())
    def test_hold_wrong_fd(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with tempfile.TemporaryFile() as tmp:
            fd = tmp.fileno()
            with self.assertRaises(lzc_exc.BadHoldCleanupFD):
                lzc.lzc_hold({snap: b'tag'}, fd)

    def test_hold_fd(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag'}, fd)

    def test_hold_empty(self):
        with cleanup_fd() as fd:
            lzc.lzc_hold({}, fd)

    def test_hold_empty_2(self):
        lzc.lzc_hold({})

    def test_hold_vs_snap_destroy(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag'}, fd)

            with self.assertRaises(lzc_exc.SnapshotDestructionFailure) as ctx:
                lzc.lzc_destroy_snaps([snap], defer=False)
            for e in ctx.exception.errors:
                self.assertIsInstance(e, lzc_exc.SnapshotIsHeld)

            lzc.lzc_destroy_snaps([snap], defer=True)
            self.assertExists(snap)

        # after automatic hold cleanup and deferred destruction
        self.assertNotExists(snap)

    def test_hold_many_tags(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag1'}, fd)
            lzc.lzc_hold({snap: b'tag2'}, fd)

    def test_hold_many_snaps(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap1: b'tag', snap2: b'tag'}, fd)

    def test_hold_many_with_one_missing(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap1])

        with cleanup_fd() as fd:
            missing = lzc.lzc_hold({snap1: b'tag', snap2: b'tag'}, fd)
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0], snap2)

    def test_hold_many_with_all_missing(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.pool.getRoot().getSnap()

        with cleanup_fd() as fd:
            missing = lzc.lzc_hold({snap1: b'tag', snap2: b'tag'}, fd)
        self.assertEqual(len(missing), 2)
        self.assertEqual(sorted(missing), sorted([snap1, snap2]))

    def test_hold_missing_fs(self):
        # XXX skip pre-created filesystems
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        snap = ZFSTest.pool.getRoot().getFilesystem().getSnap()

        snaps = lzc.lzc_hold({snap: b'tag'})
        self.assertEqual([snap], snaps)

    def test_hold_missing_fs_auto_cleanup(self):
        # XXX skip pre-created filesystems
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        ZFSTest.pool.getRoot().getFilesystem()
        snap = ZFSTest.pool.getRoot().getFilesystem().getSnap()

        with cleanup_fd() as fd:
            snaps = lzc.lzc_hold({snap: b'tag'}, fd)
            self.assertEqual([snap], snaps)

    def test_hold_duplicate(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag'}, fd)
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.HoldExists)

    def test_hold_across_pools(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.misc_pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap1: b'tag', snap2: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolsDiffer)

    def test_hold_too_long_tag(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        tag = b't' * 256
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: tag}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertEqual(e.name, tag)

    # Apparently the full snapshot name is not checked for length
    # and this snapshot is treated as simply missing.
    @unittest.expectedFailure
    def test_hold_too_long_snap_name(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(False)
        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertEqual(e.name, snap)

    def test_hold_too_long_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(True)
        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertEqual(e.name, snap)

    def test_hold_invalid_snap_name(self):
        snap = ZFSTest.pool.getRoot().getSnap() + b'@bad'
        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)
            self.assertEqual(e.name, snap)

    def test_hold_invalid_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getFilesystem().getName()
        with cleanup_fd() as fd:
            with self.assertRaises(lzc_exc.HoldFailure) as ctx:
                lzc.lzc_hold({snap: b'tag'}, fd)
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)
            self.assertEqual(e.name, snap)

    def test_get_holds(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag1'}, fd)
            lzc.lzc_hold({snap: b'tag2'}, fd)

            holds = lzc.lzc_get_holds(snap)
            self.assertEqual(len(holds), 2)
            self.assertIn(b'tag1', holds)
            self.assertIn(b'tag2', holds)
            self.assertIsInstance(holds[b'tag1'], (int, int))

    def test_get_holds_after_auto_cleanup(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag1'}, fd)
            lzc.lzc_hold({snap: b'tag2'}, fd)

        holds = lzc.lzc_get_holds(snap)
        self.assertEqual(len(holds), 0)
        self.assertIsInstance(holds, dict)

    def test_get_holds_nonexistent_snap(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        with self.assertRaises(lzc_exc.SnapshotNotFound):
            lzc.lzc_get_holds(snap)

    def test_get_holds_too_long_snap_name(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(False)
        with self.assertRaises(lzc_exc.NameTooLong):
            lzc.lzc_get_holds(snap)

    def test_get_holds_too_long_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(True)
        with self.assertRaises(lzc_exc.NameTooLong):
            lzc.lzc_get_holds(snap)

    def test_get_holds_invalid_snap_name(self):
        snap = ZFSTest.pool.getRoot().getSnap() + b'@bad'
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_get_holds(snap)

    # A filesystem-like snapshot name is not recognized as
    # an invalid name.
    @unittest.expectedFailure
    def test_get_holds_invalid_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getFilesystem().getName()
        with self.assertRaises(lzc_exc.NameInvalid):
            lzc.lzc_get_holds(snap)

    def test_release_hold(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        lzc.lzc_hold({snap: b'tag'})
        ret = lzc.lzc_release({snap: [b'tag']})
        self.assertEqual(len(ret), 0)

    def test_release_hold_empty(self):
        ret = lzc.lzc_release({})
        self.assertEqual(len(ret), 0)

    def test_release_hold_complex(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.pool.getRoot().getSnap()
        snap3 = ZFSTest.pool.getRoot().getFilesystem().getSnap()
        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2, snap3])

        lzc.lzc_hold({snap1: b'tag1'})
        lzc.lzc_hold({snap1: b'tag2'})
        lzc.lzc_hold({snap2: b'tag'})
        lzc.lzc_hold({snap3: b'tag1'})
        lzc.lzc_hold({snap3: b'tag2'})

        holds = lzc.lzc_get_holds(snap1)
        self.assertEqual(len(holds), 2)
        holds = lzc.lzc_get_holds(snap2)
        self.assertEqual(len(holds), 1)
        holds = lzc.lzc_get_holds(snap3)
        self.assertEqual(len(holds), 2)

        release = {
            snap1: [b'tag1', b'tag2'],
            snap2: [b'tag'],
            snap3: [b'tag2'],
        }
        ret = lzc.lzc_release(release)
        self.assertEqual(len(ret), 0)

        holds = lzc.lzc_get_holds(snap1)
        self.assertEqual(len(holds), 0)
        holds = lzc.lzc_get_holds(snap2)
        self.assertEqual(len(holds), 0)
        holds = lzc.lzc_get_holds(snap3)
        self.assertEqual(len(holds), 1)

        ret = lzc.lzc_release({snap3: [b'tag1']})
        self.assertEqual(len(ret), 0)
        holds = lzc.lzc_get_holds(snap3)
        self.assertEqual(len(holds), 0)

    def test_release_hold_before_auto_cleanup(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag'}, fd)
            ret = lzc.lzc_release({snap: [b'tag']})
            self.assertEqual(len(ret), 0)

    def test_release_hold_and_snap_destruction(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag1'}, fd)
            lzc.lzc_hold({snap: b'tag2'}, fd)

            lzc.lzc_destroy_snaps([snap], defer=True)
            self.assertExists(snap)

            lzc.lzc_release({snap: [b'tag1']})
            self.assertExists(snap)

            lzc.lzc_release({snap: [b'tag2']})
            self.assertNotExists(snap)

    def test_release_hold_and_multiple_snap_destruction(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap: b'tag'}, fd)

            lzc.lzc_destroy_snaps([snap], defer=True)
            self.assertExists(snap)

            lzc.lzc_destroy_snaps([snap], defer=True)
            self.assertExists(snap)

            lzc.lzc_release({snap: [b'tag']})
            self.assertNotExists(snap)

    def test_release_hold_missing_tag(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap])

        ret = lzc.lzc_release({snap: [b'tag']})
        self.assertEqual(len(ret), 1)
        self.assertEqual(ret[0], snap + b'#tag')

    def test_release_hold_missing_snap(self):
        snap = ZFSTest.pool.getRoot().getSnap()

        ret = lzc.lzc_release({snap: [b'tag']})
        self.assertEqual(len(ret), 1)
        self.assertEqual(ret[0], snap)

    def test_release_hold_missing_snap_2(self):
        snap = ZFSTest.pool.getRoot().getSnap()

        ret = lzc.lzc_release({snap: [b'tag', b'another']})
        self.assertEqual(len(ret), 1)
        self.assertEqual(ret[0], snap)

    def test_release_hold_across_pools(self):
        snap1 = ZFSTest.pool.getRoot().getSnap()
        snap2 = ZFSTest.misc_pool.getRoot().getSnap()
        lzc.lzc_snapshot([snap1])
        lzc.lzc_snapshot([snap2])

        with cleanup_fd() as fd:
            lzc.lzc_hold({snap1: b'tag'}, fd)
            lzc.lzc_hold({snap2: b'tag'}, fd)
            with self.assertRaises(lzc_exc.HoldReleaseFailure) as ctx:
                lzc.lzc_release({snap1: [b'tag'], snap2: [b'tag']})
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.PoolsDiffer)

    # Apparently the tag name is not verified,
    # only its existence is checked.
    @unittest.expectedFailure
    def test_release_hold_too_long_tag(self):
        snap = ZFSTest.pool.getRoot().getSnap()
        tag = b't' * 256
        lzc.lzc_snapshot([snap])

        with self.assertRaises(lzc_exc.HoldReleaseFailure):
            lzc.lzc_release({snap: [tag]})

    # Apparently the full snapshot name is not checked for length
    # and this snapshot is treated as simply missing.
    @unittest.expectedFailure
    def test_release_hold_too_long_snap_name(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(False)

        with self.assertRaises(lzc_exc.HoldReleaseFailure):
            lzc.lzc_release({snap: [b'tag']})

    def test_release_hold_too_long_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getTooLongSnap(True)
        with self.assertRaises(lzc_exc.HoldReleaseFailure) as ctx:
            lzc.lzc_release({snap: [b'tag']})
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameTooLong)
            self.assertEqual(e.name, snap)

    def test_release_hold_invalid_snap_name(self):
        snap = ZFSTest.pool.getRoot().getSnap() + b'@bad'
        with self.assertRaises(lzc_exc.HoldReleaseFailure) as ctx:
            lzc.lzc_release({snap: [b'tag']})
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)
            self.assertEqual(e.name, snap)

    def test_release_hold_invalid_snap_name_2(self):
        snap = ZFSTest.pool.getRoot().getFilesystem().getName()
        with self.assertRaises(lzc_exc.HoldReleaseFailure) as ctx:
            lzc.lzc_release({snap: [b'tag']})
        for e in ctx.exception.errors:
            self.assertIsInstance(e, lzc_exc.NameInvalid)
            self.assertEqual(e.name, snap)

    def test_sync_missing_pool(self):
        pool = b"nonexistent"
        with self.assertRaises(lzc_exc.PoolNotFound):
            lzc.lzc_sync(pool)

    def test_sync_pool_forced(self):
        pool = ZFSTest.pool.getRoot().getName()
        lzc.lzc_sync(pool, True)

    def test_reopen_missing_pool(self):
        pool = b"nonexistent"
        with self.assertRaises(lzc_exc.PoolNotFound):
            lzc.lzc_reopen(pool)

    def test_reopen_pool_no_restart(self):
        pool = ZFSTest.pool.getRoot().getName()
        lzc.lzc_reopen(pool, False)

    def test_channel_program_missing_pool(self):
        pool = b"nonexistent"
        with self.assertRaises(lzc_exc.PoolNotFound):
            lzc.lzc_channel_program(pool, b"return {}")

    def test_channel_program_timeout(self):
        pool = ZFSTest.pool.getRoot().getName()
        zcp = b"""
for i = 1,10000 do
    zfs.sync.snapshot('""" + pool + b"""@zcp' .. i)
end
"""
        with self.assertRaises(lzc_exc.ZCPTimeout):
            lzc.lzc_channel_program(pool, zcp, instrlimit=1)

    def test_channel_program_memory_limit(self):
        pool = ZFSTest.pool.getRoot().getName()
        zcp = b"""
for i = 1,10000 do
    zfs.sync.snapshot('""" + pool + b"""@zcp' .. i)
end
"""
        with self.assertRaises(lzc_exc.ZCPSpaceError):
            lzc.lzc_channel_program(pool, zcp, memlimit=1)

    def test_channel_program_invalid_limits(self):
        pool = ZFSTest.pool.getRoot().getName()
        zcp = b"""
return {}
"""
        with self.assertRaises(lzc_exc.ZCPLimitInvalid):
            lzc.lzc_channel_program(pool, zcp, instrlimit=0)
        with self.assertRaises(lzc_exc.ZCPLimitInvalid):
            lzc.lzc_channel_program(pool, zcp, memlimit=0)

    def test_channel_program_syntax_error(self):
        pool = ZFSTest.pool.getRoot().getName()
        zcp = b"""
inv+val:id
"""
        with self.assertRaises(lzc_exc.ZCPSyntaxError) as ctx:
            lzc.lzc_channel_program(pool, zcp)
        self.assertTrue(b"syntax error" in ctx.exception.details)

    def test_channel_program_sync_snapshot(self):
        pool = ZFSTest.pool.getRoot().getName()
        snapname = ZFSTest.pool.makeName(b"@zcp")
        zcp = b"""
zfs.sync.snapshot('""" + snapname + b"""')
"""
        lzc.lzc_channel_program(pool, zcp)
        self.assertExists(snapname)

    def test_channel_program_runtime_error(self):
        pool = ZFSTest.pool.getRoot().getName()

        # failing an assertion raises a runtime error
        with self.assertRaises(lzc_exc.ZCPRuntimeError) as ctx:
            lzc.lzc_channel_program(pool, b"assert(1 == 2)")
        self.assertTrue(
            b"assertion failed" in ctx.exception.details)
        # invoking the error() function raises a runtime error
        with self.assertRaises(lzc_exc.ZCPRuntimeError) as ctx:
            lzc.lzc_channel_program(pool, b"error()")

    def test_channel_program_nosync_runtime_error(self):
        pool = ZFSTest.pool.getRoot().getName()
        zcp = b"""
zfs.sync.snapshot('""" + pool + b"""@zcp')
"""
        # lzc_channel_program_nosync() allows only "read-only" operations
        with self.assertRaises(lzc_exc.ZCPRuntimeError) as ctx:
            lzc.lzc_channel_program_nosync(pool, zcp)
        self.assertTrue(
            b"running functions from the zfs.sync" in ctx.exception.details)

    def test_change_key_new(self):
        with encrypted_filesystem() as (fs, _):
            lzc.lzc_change_key(
                fs, 'new_key',
                props={b"keyformat": lzc.zfs_keyformat.ZFS_KEYFORMAT_RAW},
                key=os.urandom(lzc.WRAPPING_KEY_LEN))

    def test_change_key_missing_fs(self):
        name = b"nonexistent"

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_change_key(
                name, 'new_key',
                props={b"keyformat": lzc.zfs_keyformat.ZFS_KEYFORMAT_RAW},
                key=os.urandom(lzc.WRAPPING_KEY_LEN))

    def test_change_key_not_loaded(self):
        with encrypted_filesystem() as (fs, _):
            lzc.lzc_unload_key(fs)
            with self.assertRaises(lzc_exc.EncryptionKeyNotLoaded):
                lzc.lzc_change_key(
                    fs, 'new_key',
                    props={b"keyformat": lzc.zfs_keyformat.ZFS_KEYFORMAT_RAW},
                    key=os.urandom(lzc.WRAPPING_KEY_LEN))

    def test_change_key_invalid_property(self):
        with encrypted_filesystem() as (fs, _):
            with self.assertRaises(lzc_exc.PropertyInvalid):
                lzc.lzc_change_key(fs, 'new_key', props={b"invalid": b"prop"})

    def test_change_key_invalid_crypt_command(self):
        with encrypted_filesystem() as (fs, _):
            with self.assertRaises(lzc_exc.UnknownCryptCommand):
                lzc.lzc_change_key(fs, 'duplicate_key')

    def test_load_key(self):
        with encrypted_filesystem() as (fs, key):
            lzc.lzc_unload_key(fs)
            lzc.lzc_load_key(fs, False, key)

    def test_load_key_invalid(self):
        with encrypted_filesystem() as (fs, key):
            lzc.lzc_unload_key(fs)
            with self.assertRaises(lzc_exc.EncryptionKeyInvalid):
                lzc.lzc_load_key(fs, False, os.urandom(lzc.WRAPPING_KEY_LEN))

    def test_load_key_already_loaded(self):
        with encrypted_filesystem() as (fs, key):
            lzc.lzc_unload_key(fs)
            lzc.lzc_load_key(fs, False, key)
            with self.assertRaises(lzc_exc.EncryptionKeyAlreadyLoaded):
                lzc.lzc_load_key(fs, False, key)

    def test_load_key_missing_fs(self):
        name = b"nonexistent"

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_load_key(name, False, key=os.urandom(lzc.WRAPPING_KEY_LEN))

    def test_unload_key(self):
        with encrypted_filesystem() as (fs, _):
            lzc.lzc_unload_key(fs)

    def test_unload_key_missing_fs(self):
        name = b"nonexistent"

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_unload_key(name)

    def test_unload_key_busy(self):
        with encrypted_filesystem() as (fs, _):
            with zfs_mount(fs):
                with self.assertRaises(lzc_exc.DatasetBusy):
                    lzc.lzc_unload_key(fs)

    def test_unload_key_not_loaded(self):
        with encrypted_filesystem() as (fs, _):
            lzc.lzc_unload_key(fs)
            with self.assertRaises(lzc_exc.EncryptionKeyNotLoaded):
                lzc.lzc_unload_key(fs)

    def test_checkpoint(self):
        pool = ZFSTest.pool.getRoot().getName()

        lzc.lzc_pool_checkpoint(pool)

    def test_checkpoint_missing_pool(self):
        pool = b"nonexistent"

        with self.assertRaises(lzc_exc.PoolNotFound):
            lzc.lzc_pool_checkpoint(pool)

    def test_checkpoint_already_exists(self):
        pool = ZFSTest.pool.getRoot().getName()

        lzc.lzc_pool_checkpoint(pool)
        with self.assertRaises(lzc_exc.CheckpointExists):
            lzc.lzc_pool_checkpoint(pool)

    def test_checkpoint_discard(self):
        pool = ZFSTest.pool.getRoot().getName()

        lzc.lzc_pool_checkpoint(pool)
        lzc.lzc_pool_checkpoint_discard(pool)

    def test_checkpoint_discard_missing_pool(self):
        pool = b"nonexistent"

        with self.assertRaises(lzc_exc.PoolNotFound):
            lzc.lzc_pool_checkpoint_discard(pool)

    def test_checkpoint_discard_missing_checkpoint(self):
        pool = ZFSTest.pool.getRoot().getName()

        with self.assertRaises(lzc_exc.CheckpointNotFound):
            lzc.lzc_pool_checkpoint_discard(pool)

    @needs_support(lzc.lzc_list_children)
    def test_list_children(self):
        name = ZFSTest.pool.makeName(b"fs1/fs")
        names = [ZFSTest.pool.makeName(b"fs1/fs/test1"),
                 ZFSTest.pool.makeName(b"fs1/fs/test2"),
                 ZFSTest.pool.makeName(b"fs1/fs/test3"), ]
        # and one snap to see that it is not listed
        snap = ZFSTest.pool.makeName(b"fs1/fs@test")

        for fs in names:
            lzc.lzc_create(fs)
        lzc.lzc_snapshot([snap])

        children = list(lzc.lzc_list_children(name))
        self.assertItemsEqual(children, names)

    @needs_support(lzc.lzc_list_children)
    def test_list_children_nonexistent(self):
        fs = ZFSTest.pool.makeName(b"nonexistent")

        with self.assertRaises(lzc_exc.DatasetNotFound):
            list(lzc.lzc_list_children(fs))

    @needs_support(lzc.lzc_list_children)
    def test_list_children_of_snap(self):
        snap = ZFSTest.pool.makeName(b"@newsnap")

        lzc.lzc_snapshot([snap])
        children = list(lzc.lzc_list_children(snap))
        self.assertEqual(children, [])

    @needs_support(lzc.lzc_list_snaps)
    def test_list_snaps(self):
        name = ZFSTest.pool.makeName(b"fs1/fs")
        names = [ZFSTest.pool.makeName(b"fs1/fs@test1"),
                 ZFSTest.pool.makeName(b"fs1/fs@test2"),
                 ZFSTest.pool.makeName(b"fs1/fs@test3"), ]
        # and one filesystem to see that it is not listed
        fs = ZFSTest.pool.makeName(b"fs1/fs/test")

        for snap in names:
            lzc.lzc_snapshot([snap])
        lzc.lzc_create(fs)

        snaps = list(lzc.lzc_list_snaps(name))
        self.assertItemsEqual(snaps, names)

    @needs_support(lzc.lzc_list_snaps)
    def test_list_snaps_nonexistent(self):
        fs = ZFSTest.pool.makeName(b"nonexistent")

        with self.assertRaises(lzc_exc.DatasetNotFound):
            list(lzc.lzc_list_snaps(fs))

    @needs_support(lzc.lzc_list_snaps)
    def test_list_snaps_of_snap(self):
        snap = ZFSTest.pool.makeName(b"@newsnap")

        lzc.lzc_snapshot([snap])
        snaps = list(lzc.lzc_list_snaps(snap))
        self.assertEqual(snaps, [])

    @needs_support(lzc.lzc_get_props)
    def test_get_fs_props(self):
        fs = ZFSTest.pool.makeName(b"new")
        props = {b"user:foo": b"bar"}

        lzc.lzc_create(fs, props=props)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset(props, actual_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_fs_props_with_child(self):
        parent = ZFSTest.pool.makeName(b"parent")
        child = ZFSTest.pool.makeName(b"parent/child")
        parent_props = {b"user:foo": b"parent"}
        child_props = {b"user:foo": b"child"}

        lzc.lzc_create(parent, props=parent_props)
        lzc.lzc_create(child, props=child_props)
        actual_parent_props = lzc.lzc_get_props(parent)
        actual_child_props = lzc.lzc_get_props(child)
        self.assertDictContainsSubset(parent_props, actual_parent_props)
        self.assertDictContainsSubset(child_props, actual_child_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_snap_props(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        snaps = [snapname]
        props = {b"user:foo": b"bar"}

        lzc.lzc_snapshot(snaps, props)
        actual_props = lzc.lzc_get_props(snapname)
        self.assertDictContainsSubset(props, actual_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_props_nonexistent(self):
        fs = ZFSTest.pool.makeName(b"nonexistent")

        with self.assertRaises(lzc_exc.DatasetNotFound):
            lzc.lzc_get_props(fs)

    @needs_support(lzc.lzc_get_props)
    def test_get_mountpoint_none(self):
        '''
        If the *mountpoint* property is set to none, then its
        value is returned as `bytes` "none".
        Also, a child filesystem inherits that value.
        '''
        fs = ZFSTest.pool.makeName(b"new")
        child = ZFSTest.pool.makeName(b"new/child")
        props = {b"mountpoint": b"none"}

        lzc.lzc_create(fs, props=props)
        lzc.lzc_create(child)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset(props, actual_props)
        # check that mountpoint value is correctly inherited
        child_props = lzc.lzc_get_props(child)
        self.assertDictContainsSubset(props, child_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_mountpoint_legacy(self):
        '''
        If the *mountpoint* property is set to legacy, then its
        value is returned as `bytes` "legacy".
        Also, a child filesystem inherits that value.
        '''
        fs = ZFSTest.pool.makeName(b"new")
        child = ZFSTest.pool.makeName(b"new/child")
        props = {b"mountpoint": b"legacy"}

        lzc.lzc_create(fs, props=props)
        lzc.lzc_create(child)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset(props, actual_props)
        # check that mountpoint value is correctly inherited
        child_props = lzc.lzc_get_props(child)
        self.assertDictContainsSubset(props, child_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_mountpoint_path(self):
        '''
        If the *mountpoint* property is set to a path and the property
        is not explicitly set on a child filesystem, then its
        value is that of the parent filesystem with the child's
        name appended using the '/' separator.
        '''
        fs = ZFSTest.pool.makeName(b"new")
        child = ZFSTest.pool.makeName(b"new/child")
        props = {b"mountpoint": b"/mnt"}

        lzc.lzc_create(fs, props=props)
        lzc.lzc_create(child)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset(props, actual_props)
        # check that mountpoint value is correctly inherited
        child_props = lzc.lzc_get_props(child)
        self.assertDictContainsSubset(
            {b"mountpoint": b"/mnt/child"}, child_props)

    @needs_support(lzc.lzc_get_props)
    def test_get_snap_clones(self):
        fs = ZFSTest.pool.makeName(b"new")
        snap = ZFSTest.pool.makeName(b"@snap")
        clone1 = ZFSTest.pool.makeName(b"clone1")
        clone2 = ZFSTest.pool.makeName(b"clone2")

        lzc.lzc_create(fs)
        lzc.lzc_snapshot([snap])
        lzc.lzc_clone(clone1, snap)
        lzc.lzc_clone(clone2, snap)

        clones_prop = lzc.lzc_get_props(snap)["clones"]
        self.assertItemsEqual(clones_prop, [clone1, clone2])

    @needs_support(lzc.lzc_rename)
    def test_rename(self):
        src = ZFSTest.pool.makeName(b"source")
        tgt = ZFSTest.pool.makeName(b"target")

        lzc.lzc_create(src)
        lzc.lzc_rename(src, tgt)
        self.assertNotExists(src)
        self.assertExists(tgt)

    @needs_support(lzc.lzc_rename)
    def test_rename_nonexistent(self):
        src = ZFSTest.pool.makeName(b"source")
        tgt = ZFSTest.pool.makeName(b"target")

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_rename(src, tgt)

    @needs_support(lzc.lzc_rename)
    def test_rename_existing_target(self):
        src = ZFSTest.pool.makeName(b"source")
        tgt = ZFSTest.pool.makeName(b"target")

        lzc.lzc_create(src)
        lzc.lzc_create(tgt)
        with self.assertRaises(lzc_exc.FilesystemExists):
            lzc.lzc_rename(src, tgt)

    @needs_support(lzc.lzc_rename)
    def test_rename_nonexistent_target_parent(self):
        src = ZFSTest.pool.makeName(b"source")
        tgt = ZFSTest.pool.makeName(b"parent/target")

        lzc.lzc_create(src)
        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_rename(src, tgt)

    @needs_support(lzc.lzc_rename)
    def test_rename_parent_is_zvol(self):
        src = ZFSTest.pool.makeName(b"source")
        zvol = ZFSTest.pool.makeName(b"parent")
        tgt = zvol + b"/target"
        props = {b"volsize": 1024 * 1024}

        lzc.lzc_create(src)
        lzc.lzc_create(zvol, ds_type='zvol', props=props)
        with self.assertRaises(lzc_exc.WrongParent):
            lzc.lzc_rename(src, tgt)

    @needs_support(lzc.lzc_destroy)
    def test_destroy(self):
        fs = ZFSTest.pool.makeName(b"test-fs")

        lzc.lzc_create(fs)
        lzc.lzc_destroy(fs)
        self.assertNotExists(fs)

    @needs_support(lzc.lzc_destroy)
    def test_destroy_nonexistent(self):
        fs = ZFSTest.pool.makeName(b"test-fs")

        with self.assertRaises(lzc_exc.FilesystemNotFound):
            lzc.lzc_destroy(fs)

    @needs_support(lzc.lzc_inherit_prop)
    def test_inherit_prop(self):
        parent = ZFSTest.pool.makeName(b"parent")
        child = ZFSTest.pool.makeName(b"parent/child")
        the_prop = b"user:foo"
        parent_props = {the_prop: b"parent"}
        child_props = {the_prop: b"child"}

        lzc.lzc_create(parent, props=parent_props)
        lzc.lzc_create(child, props=child_props)
        lzc.lzc_inherit_prop(child, the_prop)
        actual_props = lzc.lzc_get_props(child)
        self.assertDictContainsSubset(parent_props, actual_props)

    @needs_support(lzc.lzc_inherit_prop)
    def test_inherit_missing_prop(self):
        parent = ZFSTest.pool.makeName(b"parent")
        child = ZFSTest.pool.makeName(b"parent/child")
        the_prop = "user:foo"
        child_props = {the_prop: "child"}

        lzc.lzc_create(parent)
        lzc.lzc_create(child, props=child_props)
        lzc.lzc_inherit_prop(child, the_prop)
        actual_props = lzc.lzc_get_props(child)
        self.assertNotIn(the_prop, actual_props)

    @needs_support(lzc.lzc_inherit_prop)
    def test_inherit_readonly_prop(self):
        parent = ZFSTest.pool.makeName(b"parent")
        child = ZFSTest.pool.makeName(b"parent/child")
        the_prop = b"createtxg"

        lzc.lzc_create(parent)
        lzc.lzc_create(child)
        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_inherit_prop(child, the_prop)

    @needs_support(lzc.lzc_inherit_prop)
    def test_inherit_unknown_prop(self):
        parent = ZFSTest.pool.makeName(b"parent")
        child = ZFSTest.pool.makeName(b"parent/child")
        the_prop = b"nosuchprop"

        lzc.lzc_create(parent)
        lzc.lzc_create(child)
        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_inherit_prop(child, the_prop)

    @needs_support(lzc.lzc_inherit_prop)
    def test_inherit_prop_on_snap(self):
        fs = ZFSTest.pool.makeName(b"new")
        snapname = ZFSTest.pool.makeName(b"new@snap")
        prop = b"user:foo"
        fs_val = b"fs"
        snap_val = b"snap"

        lzc.lzc_create(fs, props={prop: fs_val})
        lzc.lzc_snapshot([snapname], props={prop: snap_val})

        actual_props = lzc.lzc_get_props(snapname)
        self.assertDictContainsSubset({prop: snap_val}, actual_props)

        lzc.lzc_inherit_prop(snapname, prop)
        actual_props = lzc.lzc_get_props(snapname)
        self.assertDictContainsSubset({prop: fs_val}, actual_props)

    @needs_support(lzc.lzc_set_prop)
    def test_set_fs_prop(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"user:foo"
        val = b"bar"

        lzc.lzc_create(fs)
        lzc.lzc_set_prop(fs, prop, val)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset({prop: val}, actual_props)

    @needs_support(lzc.lzc_set_prop)
    def test_set_snap_prop(self):
        snapname = ZFSTest.pool.makeName(b"@snap")
        prop = b"user:foo"
        val = b"bar"

        lzc.lzc_snapshot([snapname])
        lzc.lzc_set_prop(snapname, prop, val)
        actual_props = lzc.lzc_get_props(snapname)
        self.assertDictContainsSubset({prop: val}, actual_props)

    @needs_support(lzc.lzc_set_prop)
    def test_set_prop_nonexistent(self):
        fs = ZFSTest.pool.makeName(b"nonexistent")
        prop = b"user:foo"
        val = b"bar"

        with self.assertRaises(lzc_exc.DatasetNotFound):
            lzc.lzc_set_prop(fs, prop, val)

    @needs_support(lzc.lzc_set_prop)
    def test_set_sys_prop(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"recordsize"
        val = 4096

        lzc.lzc_create(fs)
        lzc.lzc_set_prop(fs, prop, val)
        actual_props = lzc.lzc_get_props(fs)
        self.assertDictContainsSubset({prop: val}, actual_props)

    @needs_support(lzc.lzc_set_prop)
    def test_set_invalid_prop(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"nosuchprop"
        val = 0

        lzc.lzc_create(fs)
        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_set_prop(fs, prop, val)

    @needs_support(lzc.lzc_set_prop)
    def test_set_invalid_value_prop(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"atime"
        val = 100

        lzc.lzc_create(fs)
        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_set_prop(fs, prop, val)

    @needs_support(lzc.lzc_set_prop)
    def test_set_invalid_value_prop_2(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"readonly"
        val = 100

        lzc.lzc_create(fs)
        with self.assertRaises(lzc_exc.PropertyInvalid):
            lzc.lzc_set_prop(fs, prop, val)

    @needs_support(lzc.lzc_set_prop)
    def test_set_prop_too_small_quota(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"refquota"
        val = 1

        lzc.lzc_create(fs)
        with self.assertRaises(lzc_exc.NoSpace):
            lzc.lzc_set_prop(fs, prop, val)

    @needs_support(lzc.lzc_set_prop)
    def test_set_readonly_prop(self):
        fs = ZFSTest.pool.makeName(b"new")
        prop = b"creation"
        val = 0

        lzc.lzc_create(fs)
        lzc.lzc_set_prop(fs, prop, val)
        actual_props = lzc.lzc_get_props(fs)
        # the change is silently ignored
        self.assertTrue(actual_props[prop] != val)


class _TempPool(object):
    SNAPSHOTS = [b'snap', b'snap1', b'snap2']
    BOOKMARKS = [b'bmark', b'bmark1', b'bmark2']

    _cachefile_suffix = ".cachefile"

    # XXX Whether to do a sloppy but much faster cleanup
    # or a proper but slower one.
    _recreate_pools = True

    def __init__(self, size=128 * 1024 * 1024, readonly=False, filesystems=[]):
        self._filesystems = filesystems
        self._readonly = readonly
        if sys.version_info < (3, 0):
            self._pool_name = b'pool.' + bytes(uuid.uuid4())
        else:
            self._pool_name = b'pool.' + bytes(str(uuid.uuid4()),
                                               encoding='utf-8')
        self._root = _Filesystem(self._pool_name)
        (fd, self._pool_file_path) = tempfile.mkstemp(
            suffix='.zpool', prefix='tmp-')
        if readonly:
            cachefile = self._pool_file_path + _TempPool._cachefile_suffix
        else:
            cachefile = 'none'
        self._zpool_create = [
            'zpool', 'create', '-o', 'cachefile=' + cachefile,
            '-O', 'mountpoint=legacy', '-O', 'compression=off',
            self._pool_name, self._pool_file_path]
        try:
            os.ftruncate(fd, size)
            os.close(fd)

            subprocess.check_output(
                self._zpool_create, stderr=subprocess.STDOUT)

            for fs in filesystems:
                lzc.lzc_create(self.makeName(fs))

            self._bmarks_supported = self.isPoolFeatureEnabled('bookmarks')

            if readonly:
                # To make a pool read-only it must exported and re-imported
                # with readonly option.
                # The most deterministic way to re-import the pool is by using
                # a cache file.
                # But the cache file has to be stashed away before the pool is
                # exported, because otherwise the pool is removed from the
                # cache.
                shutil.copyfile(cachefile, cachefile + '.tmp')
                subprocess.check_output(
                    ['zpool', 'export', '-f', self._pool_name],
                    stderr=subprocess.STDOUT)
                os.rename(cachefile + '.tmp', cachefile)
                subprocess.check_output(
                    ['zpool', 'import', '-f', '-N', '-c', cachefile,
                        '-o', 'readonly=on', self._pool_name],
                    stderr=subprocess.STDOUT)
                os.remove(cachefile)

        except subprocess.CalledProcessError as e:
            self.cleanUp()
            if b'permission denied' in e.output:
                raise unittest.SkipTest(
                    'insufficient privileges to run libzfs_core tests')
            print('command failed: ', e.output)
            raise
        except Exception:
            self.cleanUp()
            raise

    def reset(self):
        if self._readonly:
            return

        if not self.__class__._recreate_pools:
            snaps = []
            for fs in [''] + self._filesystems:
                for snap in self.__class__.SNAPSHOTS:
                    snaps.append(self.makeName(fs + '@' + snap))
            self.getRoot().visitSnaps(lambda snap: snaps.append(snap))
            lzc.lzc_destroy_snaps(snaps, defer=False)

            if self._bmarks_supported:
                bmarks = []
                for fs in [''] + self._filesystems:
                    for bmark in self.__class__.BOOKMARKS:
                        bmarks.append(self.makeName(fs + '#' + bmark))
                self.getRoot().visitBookmarks(
                    lambda bmark: bmarks.append(bmark))
                lzc.lzc_destroy_bookmarks(bmarks)
            self.getRoot().reset()
            return

        # On the Buildbot builders this may fail with "pool is busy"
        # Retry 5 times before raising an error
        retry = 0
        while True:
            try:
                subprocess.check_output(
                    ['zpool', 'destroy', '-f', self._pool_name],
                    stderr=subprocess.STDOUT)
                subprocess.check_output(
                    self._zpool_create, stderr=subprocess.STDOUT)
                break
            except subprocess.CalledProcessError as e:
                if b'pool is busy' in e.output and retry < 5:
                    retry += 1
                    time.sleep(1)
                    continue
                else:
                    print('command failed: ', e.output)
                    raise
        for fs in self._filesystems:
            lzc.lzc_create(self.makeName(fs))
        self.getRoot().reset()

    def cleanUp(self):
        try:
            subprocess.check_output(
                ['zpool', 'destroy', '-f', self._pool_name],
                stderr=subprocess.STDOUT)
        except Exception:
            pass
        try:
            os.remove(self._pool_file_path)
        except Exception:
            pass
        try:
            os.remove(self._pool_file_path + _TempPool._cachefile_suffix)
        except Exception:
            pass
        try:
            os.remove(
                self._pool_file_path + _TempPool._cachefile_suffix + '.tmp')
        except Exception:
            pass

    def makeName(self, relative=None):
        if not relative:
            return self._pool_name
        if relative.startswith((b'@', b'#')):
            return self._pool_name + relative
        return self._pool_name + b'/' + relative

    def makeTooLongName(self, prefix=None):
        if not prefix:
            prefix = b'x'
        prefix = self.makeName(prefix)
        pad_len = lzc.MAXNAMELEN + 1 - len(prefix)
        if pad_len > 0:
            return prefix + b'x' * pad_len
        else:
            return prefix

    def makeTooLongComponent(self, prefix=None):
        padding = b'x' * (lzc.MAXNAMELEN + 1)
        if not prefix:
            prefix = padding
        else:
            prefix = prefix + padding
        return self.makeName(prefix)

    def getRoot(self):
        return self._root

    def getFilesystem(self, fsname):
        return _Filesystem(self._pool_name + b'/' + fsname)

    def isPoolFeatureAvailable(self, feature):
        output = subprocess.check_output(
            ['zpool', 'get', '-H', 'feature@' + feature, self._pool_name])
        output = output.strip()
        return output != ''

    def isPoolFeatureEnabled(self, feature):
        output = subprocess.check_output(
            ['zpool', 'get', '-H', 'feature@' + feature, self._pool_name])
        output = output.split()[2]
        return output in [b'active', b'enabled']


class _Filesystem(object):

    def __init__(self, name):
        self._name = name
        self.reset()

    def getName(self):
        return self._name

    def reset(self):
        self._children = []
        self._fs_id = 0
        self._snap_id = 0
        self._bmark_id = 0

    def getFilesystem(self):
        self._fs_id += 1
        fsname = self._name + b'/fs' + str(self._fs_id).encode()
        fs = _Filesystem(fsname)
        self._children.append(fs)
        return fs

    def getProperty(self, propname, received=False):
        if received:
            output = subprocess.check_output(
                ['zfs', 'get', '-pH', '-o', 'received', propname, self._name])
        else:
            output = subprocess.check_output(
                ['zfs', 'get', '-pH', '-o', 'value', propname, self._name])
        return output.strip()

    def _makeSnapName(self, i):
        return self._name + b'@snap' + str(i).encode()

    def getSnap(self):
        self._snap_id += 1
        return self._makeSnapName(self._snap_id)

    def _makeBookmarkName(self, i):
        return self._name + b'#bmark' + bytes(i)

    def getBookmark(self):
        self._bmark_id += 1
        return self._makeBookmarkName(self._bmark_id)

    def _makeTooLongName(self, too_long_component):
        if too_long_component:
            return b'x' * (lzc.MAXNAMELEN + 1)

        # Note that another character is used for one of '/', '@', '#'.
        comp_len = lzc.MAXNAMELEN - len(self._name)
        if comp_len > 0:
            return b'x' * comp_len
        else:
            return b'x'

    def getTooLongFilesystemName(self, too_long_component):
        return self._name + b'/' + self._makeTooLongName(too_long_component)

    def getTooLongSnap(self, too_long_component):
        return self._name + b'@' + self._makeTooLongName(too_long_component)

    def getTooLongBookmark(self, too_long_component):
        return self._name + b'#' + self._makeTooLongName(too_long_component)

    def _visitFilesystems(self, visitor):
        for child in self._children:
            child._visitFilesystems(visitor)
        visitor(self)

    def visitFilesystems(self, visitor):
        def _fsVisitor(fs):
            visitor(fs._name)

        self._visitFilesystems(_fsVisitor)

    def visitSnaps(self, visitor):
        def _snapVisitor(fs):
            for i in range(1, fs._snap_id + 1):
                visitor(fs._makeSnapName(i))

        self._visitFilesystems(_snapVisitor)

    def visitBookmarks(self, visitor):
        def _bmarkVisitor(fs):
            for i in range(1, fs._bmark_id + 1):
                visitor(fs._makeBookmarkName(i))

        self._visitFilesystems(_bmarkVisitor)


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
