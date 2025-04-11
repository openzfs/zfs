import unittest
import os
import pwd
import shutil
import libzfsacl
import sys
from subprocess import run, PIPE


def run_as_user(cmd, user):
    if shutil.which(cmd.split()[0]) is not None:
        cmd = shutil.which(cmd.split()[0]) + " " + " ".join(cmd.split()[1:])
    command = ["/usr/bin/su", "-", user, "-c", cmd]
    proc = run(command, stdout=PIPE, stderr=PIPE,
               universal_newlines=True, timeout=30)
    if proc.returncode != 0:
        return {"result": False, "output": proc.stdout,
                "error": proc.stderr, "returncode": proc.returncode}
    else:
        return {"result": True, "output": proc.stdout,
                "error": proc.stderr, "returncode": proc.returncode}


class TestNFSAcl(unittest.TestCase):

    ZFS_ACL_STAFF_GROUP = "zfsgrp"
    ZFS_ACL_STAFF1 = "staff1"
    ZFS_ACL_STAFF2 = "staff2"
    ZFS_ACL_STAFF1_UID = 0
    ZFS_ACL_STAFF2_UID = 0
    MOUNTPT = "/var/tmp/testdir"
    TESTPOOL = "testpool"
    TESTFS = "testfs"
    TDIR = '/var/tmp/testdir/test'
    USER_OBJ_PERMSET = libzfsacl.PERM_READ_DATA | \
        libzfsacl.PERM_LIST_DIRECTORY | libzfsacl.PERM_WRITE_DATA | \
        libzfsacl.PERM_ADD_FILE | libzfsacl.PERM_APPEND_DATA | \
        libzfsacl.PERM_ADD_SUBDIRECTORY | libzfsacl.PERM_READ_ATTRIBUTES | \
        libzfsacl.PERM_WRITE_ATTRIBUTES | libzfsacl.PERM_READ_NAMED_ATTRS | \
        libzfsacl.PERM_WRITE_NAMED_ATTRS | libzfsacl.PERM_READ_ACL | \
        libzfsacl.PERM_WRITE_ACL | libzfsacl.PERM_WRITE_OWNER | \
        libzfsacl.PERM_SYNCHRONIZE
    OMIT_PERMSET = libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA | \
        libzfsacl.PERM_DELETE_CHILD | libzfsacl.PERM_READ_ATTRIBUTES | \
        libzfsacl.PERM_WRITE_ATTRIBUTES | libzfsacl.PERM_DELETE | \
        libzfsacl.PERM_READ_ACL | libzfsacl.PERM_WRITE_ACL | \
        libzfsacl.PERM_WRITE_OWNER | libzfsacl.PERM_EXECUTE

    # Init UIDs for ZFS users
    def __init__(self, *args, **kwargs):
        self.ZFS_ACL_STAFF1_UID = pwd.getpwnam(self.ZFS_ACL_STAFF1).pw_uid
        self.ZFS_ACL_STAFF2_UID = pwd.getpwnam(self.ZFS_ACL_STAFF2).pw_uid
        super(TestNFSAcl, self).__init__(*args, **kwargs)

    # Test pool ACL type is NFSv4
    def test_001_pool_acl_type(self):
        acl = libzfsacl.Acl(path=f"/{self.TESTPOOL}")
        self.assertEqual(libzfsacl.BRAND_NFSV4, acl.brand,
                         "ACL type is not NFSv4")

    # Test dataset mountpoint ACL type is NFSv4
    def test_002_fs_acl_type(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        self.assertEqual(libzfsacl.BRAND_NFSV4, acl.brand,
                         "ACL type is not NFSv4")

    # Test default ACE count
    def test_003_default_ace_count(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        self.assertEqual(3, acl.ace_count, "Default ace count is not 3")

    # Try to get first ACE
    def test_004_get_first_ace(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        entry0 = acl.get_entry(0)
        self.assertEqual(0, entry0.idx, "Failed to get first ACE")

    # Try to get last ACE
    def test_005_get_last_ace(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        entry0 = acl.get_entry(acl.ace_count - 1)
        self.assertEqual(acl.ace_count - 1, entry0.idx,
                         "Failed to get last ACE")

    # Test default USER_OBJ ACE is present
    def test_006_default_ace_user_obj(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        entry0 = acl.get_entry(0)
        self.assertEqual(0, entry0.idx, "Default ACE 0 idx is not 0")
        self.assertEqual(libzfsacl.ENTRY_TYPE_ALLOW, entry0.entry_type,
                         "Default ACE 0 is not ENTRY_TYPE_ALLOW")
        self.assertEqual(0, entry0.flagset,
                         "Default ACE 0 flagset is not NO_INHERIT")
        self.assertEqual(libzfsacl.WHOTYPE_USER_OBJ, entry0.who[0],
                         "ACE 0 who type is not USER_OBJ")
        self.assertEqual(self.USER_OBJ_PERMSET,
                         entry0.permset & self.USER_OBJ_PERMSET,
                         "Default ACE 0 permset does not match"
                         "USER_OBJ_PERMSET")

    # Test default GROUP_OBJ ACE is present
    def test_007_default_ace_group_obj(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        entry1 = acl.get_entry(1)
        self.assertEqual(1, entry1.idx, "Default ACE 1 idx is not 1")
        self.assertEqual(libzfsacl.ENTRY_TYPE_ALLOW, entry1.entry_type,
                         "Default ACE 1 is not ENTRY_TYPE_ALLOW")
        self.assertEqual(libzfsacl.WHOTYPE_GROUP_OBJ, entry1.who[0],
                         "ACE 1 who type is not GROUP_OBJ")

    # Test default EVERYONE ACE is present
    def test_008_default_ace_everyone(self):
        acl = libzfsacl.Acl(path=self.MOUNTPT)
        entry2 = acl.get_entry(2)
        self.assertEqual(2, entry2.idx, "Default ACE 2 idx is not 1")
        self.assertEqual(libzfsacl.ENTRY_TYPE_ALLOW, entry2.entry_type,
                         "Default ACE 2 is not ENTRY_TYPE_ALLOW")
        self.assertEqual(0, entry2.flagset,
                         "Default ACE 2 flagset is not NO_INHERIT")
        self.assertEqual(libzfsacl.WHOTYPE_EVERYONE, entry2.who[0],
                         "ACE 2 who type is not EVERYONE")

    # Test an ACE can be appended
    def test_009_append_an_ace(self):
        os.makedirs(self.TDIR)
        dacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = dacl.create_entry()
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        dacl.setacl(path=self.TDIR)
        dacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = dacl.get_entry(dacl.ace_count - 1)
        os.rmdir(self.TDIR)
        self.assertEqual(newEntry.entry_type,
                         libzfsacl.ENTRY_TYPE_ALLOW,
                         "New ACE is not ENTRY_TYPE_ALLOW")
        self.assertEqual(newEntry.who[0], libzfsacl.WHOTYPE_USER,
                         "New ACE who type is not WHOTYPE_USER")
        self.assertEqual(newEntry.who[1], self.ZFS_ACL_STAFF1_UID,
                         "New ACE who user is not ZFS_ACL_STAFF1_UID")

    # Test an ACE can be prepended
    def test_010_prepend_an_ace(self):
        os.makedirs(self.TDIR)
        dacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = dacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        dacl.setacl(path=self.TDIR)
        dacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = dacl.get_entry(0)
        os.rmdir(self.TDIR)
        self.assertEqual(newEntry.entry_type,
                         libzfsacl.ENTRY_TYPE_ALLOW,
                         "New ACE is not ENTRY_TYPE_ALLOW")
        self.assertEqual(newEntry.who[0], libzfsacl.WHOTYPE_USER,
                         "New ACE who type is not WHOTYPE_USER")
        self.assertEqual(newEntry.who[1], self.ZFS_ACL_STAFF1_UID,
                         "New ACE who user is not ZFS_ACL_STAFF1_UID")

    # Test DENY ace can be set
    def test_011_add_ace_set_entry_type_deny(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        tdacl.setacl(path=self.TDIR)
        tdacl_entry0 = libzfsacl.Acl(path=self.TDIR).get_entry(0)
        os.rmdir(self.TDIR)
        self.assertEqual(libzfsacl.ENTRY_TYPE_DENY, tdacl_entry0.entry_type,
                         "Failed to add deny ACE")

    # Test ALLOW ace can be set
    def test_012_add_ace_set_entry_type_allow(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        tdacl.setacl(path=self.TDIR)
        tdacl_entry0 = libzfsacl.Acl(path=self.TDIR).get_entry(0)
        os.rmdir(self.TDIR)
        self.assertEqual(libzfsacl.ENTRY_TYPE_ALLOW, tdacl_entry0.entry_type,
                         "Failed to add allow ACE")

    # Test adding an ACE works on mountpoint
    def test_013_add_ace_mountpoint(self):
        mpacl = libzfsacl.Acl(path=self.MOUNTPT)
        orig_cnt = mpacl.ace_count
        newEntry = mpacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        mpacl.setacl(path=self.MOUNTPT)
        self.assertEqual(orig_cnt + 1, mpacl.ace_count,
                         "Failed to add an ACE on mountpoint")

    # Test removing an ACE works on mountpoint
    def test_014_remove_ace_mountpoint(self):
        mpacl = libzfsacl.Acl(path=self.MOUNTPT)
        orig_cnt = mpacl.ace_count
        mpacl.delete_entry(0)
        self.assertEqual(orig_cnt - 1, mpacl.ace_count,
                         "Failed to delete an ACE from mountpoint")

    # Test adding an ACE works on a directory
    def test_015_add_ace_dir(self):
        os.makedirs(self.TDIR)
        dacl = libzfsacl.Acl(path=self.TDIR)
        orig_cnt = dacl.ace_count
        newEntry = dacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        dacl.setacl(path=self.TDIR)
        self.assertEqual(orig_cnt + 1, dacl.ace_count,
                         "Failed to add an ACE on a directory")

    # Test removing an ace from a directory
    def test_016_remove_ace_dir(self):
        dacl = libzfsacl.Acl(path=self.TDIR)
        orig_cnt = dacl.ace_count
        dacl.delete_entry(0)
        new_cnt = dacl.ace_count
        os.rmdir(self.TDIR)
        self.assertEqual(orig_cnt - 1, new_cnt,
                         "Failed to delete an ACE from a directory")

    # Test adding an ACE to a file
    def test_017_add_ace_file(self):
        tfile = f'{self.MOUNTPT}/test.txt'
        with open(tfile, 'w'):
            pass
        facl = libzfsacl.Acl(path=tfile)
        orig_cnt = facl.ace_count
        newEntry = facl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        facl.setacl(path=tfile)
        self.assertEqual(orig_cnt + 1, facl.ace_count,
                         "Failed to add an ACE to a file")

    # Test removing an ace from a file
    def test_018_remove_ace_file(self):
        tfile = f'{self.MOUNTPT}/test.txt'
        facl = libzfsacl.Acl(path=tfile)
        orig_cnt = facl.ace_count
        facl.delete_entry(0)
        new_cnt = facl.ace_count
        os.remove(tfile)
        self.assertEqual(orig_cnt - 1, new_cnt,
                         "Failed to delete an ACE from a file")

    # Test a flag can be set on file
    def test_019_basic_flagset(self):
        tfile = f'{self.MOUNTPT}/test.txt'
        with open(tfile, 'w'):
            pass
        facl = libzfsacl.Acl(path=tfile)
        newEntry = facl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        facl.setacl(path=tfile)
        facl = libzfsacl.Acl(path=tfile)
        facl_entry0 = facl.get_entry(0)
        os.remove(tfile)
        self.assertEqual(facl_entry0.flagset, 0,
                         "Failed to set basic flagset")

    # Test multiple flags can be set on directory
    def test_020_advanced_flagset(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        adv_flags = libzfsacl.FLAG_FILE_INHERIT | \
            libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_NO_PROPAGATE_INHERIT | \
            libzfsacl.FLAG_INHERIT_ONLY
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = adv_flags
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tdacl.setacl(path=self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl_entry0 = tdacl.get_entry(0)
        os.rmdir(self.TDIR)
        self.assertEqual(tdacl_entry0.flagset, adv_flags,
                         "FLAG_INHERITED is set by default.")

    # Test no inherited ace is present by default
    def test_021_flagset_no_inherited_ace_by_default(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        not_inherited = 0
        for i in range(tdacl.ace_count):
            if (tdacl.get_entry(i).flagset & libzfsacl.FLAG_INHERITED) == 0:
                not_inherited += 1
        os.rmdir(self.TDIR)
        self.assertEqual(not_inherited, tdacl.ace_count,
                         "FLAG_INHERITED is set by default.")

    # Test FILE_INHERIT flag functions correctly
    def test_022_flagset_file_inherit(self):
        tfile = f'{self.TDIR}/test_file.txt'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = newEntry.flagset | libzfsacl.FLAG_FILE_INHERIT
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tdacl.setacl(path=self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl_entry0 = tfacl.get_entry(0)
        shutil.rmtree(self.TDIR)
        self.assertEqual(libzfsacl.FLAG_INHERITED, tfacl_entry0.flagset,
                         "libzfsacl.FLAG_INHERITED is not set")

    # Test DIRECTORY_INHERIT functions correctly
    def test_023_flagset_directory_inherit(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = newEntry.flagset | libzfsacl.FLAG_DIRECTORY_INHERIT
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tdacl.setacl(path=self.TDIR)
        os.makedirs(tddir)
        tfacl = libzfsacl.Acl(path=tddir)
        tfacl_entry0 = tfacl.get_entry(0)
        shutil.rmtree(self.TDIR)
        self.assertEqual(libzfsacl.FLAG_INHERITED |
                         libzfsacl.FLAG_DIRECTORY_INHERIT,
                         tfacl_entry0.flagset,
                         "libzfsacl.FLAG_DIRECTORY_INHERIT is not set")

    # Test NO_PROPAGATE_INHERIT functions correctly
    def test_024_flagset_no_propagate_inherit(self):
        tddir = f'{self.TDIR}/test_dir'
        ttdir = f'{tddir}/test'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = newEntry.flagset | \
            libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_NO_PROPAGATE_INHERIT
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tdacl.setacl(path=self.TDIR)
        os.makedirs(tddir)
        os.makedirs(ttdir)
        ttdacl = libzfsacl.Acl(path=ttdir)
        not_inherited = 0
        for i in range(ttdacl.ace_count):
            if ttdacl.get_entry(i).flagset & libzfsacl.FLAG_INHERITED == 0:
                not_inherited += 1
        shutil.rmtree(self.TDIR)
        self.assertEqual(ttdacl.ace_count, not_inherited,
                         "libzfsacl.FLAG_NO_PROPAGATE_INHERIT is not "
                         "functioning properly")

    # Test INHERIT_ONLY flag behavior on dirs, if DIRECTORY_INHERIT was
    # set with INHERIT_ONLY, it is removed from child dirs. If not,
    # INHERIT_ONLY should be set on shild dirs.
    def test_025_flagset_inherit_only_dir(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_FILE_INHERIT | \
            libzfsacl.FLAG_INHERIT_ONLY
        newEntry.permset = libzfsacl.PERM_READ_DATA | \
            libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        os.makedirs(tddir)
        tddacl = libzfsacl.Acl(path=tddir)
        tdentry0 = tddacl.get_entry(0)
        tflags = libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_FILE_INHERIT | libzfsacl.FLAG_INHERITED
        self.assertEqual(tdentry0.idx, 0,
                         "Idx of inherited ACE at index 0 should be 0")
        self.assertEqual(tdentry0.entry_type, libzfsacl.ENTRY_TYPE_ALLOW,
                         "Inherited ACE at index 0 should be of type allow")
        self.assertEqual(tdentry0.who,
                         (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID),
                         "Inherited ACE who is not correct")
        self.assertEqual(tdentry0.flagset, tflags,
                         "Flagset on inherited ACE are not correct")
        self.assertEqual(tdentry0.permset,
                         libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA,
                         "Permse of inherited ACE at index 0 are not correct")
        os.rmdir(tddir)
        tdacl.delete_entry(0)
        tdacl.setacl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = libzfsacl.FLAG_FILE_INHERIT | \
            libzfsacl.FLAG_INHERIT_ONLY
        newEntry.permset = libzfsacl.PERM_READ_DATA | \
            libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        os.makedirs(tddir)
        tddacl = libzfsacl.Acl(path=tddir)
        tdentry0 = tddacl.get_entry(0)
        shutil.rmtree(self.TDIR)
        tflags = libzfsacl.FLAG_FILE_INHERIT | libzfsacl.FLAG_INHERITED | \
            libzfsacl.FLAG_INHERIT_ONLY
        self.assertEqual(tdentry0.idx, 0,
                         "Idx of inherited ACE at index 0 should be 0")
        self.assertEqual(tdentry0.entry_type, libzfsacl.ENTRY_TYPE_ALLOW,
                         "Inherited ACE at index 0 should be of type allow")
        self.assertEqual(tdentry0.who,
                         (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID),
                         "Inherited ACE who is not correct")
        self.assertEqual(tdentry0.flagset, tflags,
                         "Flagset on inherited ACE are not correct")
        self.assertEqual(tdentry0.permset,
                         libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA,
                         "Permse of inherited ACE at index 0 are not correct")

    # Test INHERIT_ONLY flag behavior on files, ACE should be inheritted
    def test_026_flagset_inherit_only_file(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_FILE_INHERIT | libzfsacl.FLAG_INHERIT_ONLY
        newEntry.permset = libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfentry0 = tfacl.get_entry(0)
        shutil.rmtree(self.TDIR)
        self.assertEqual(tfentry0.idx, 0,
                         "Idx of inherited ACE at index 0 should be 0")
        self.assertEqual(tfentry0.entry_type, libzfsacl.ENTRY_TYPE_ALLOW,
                         "Inherited ACE at index 0 should be of type allow")
        self.assertEqual(tfentry0.who,
                         (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID),
                         "Inherited ACE who is not correct")
        self.assertEqual(tfentry0.flagset, libzfsacl.FLAG_INHERITED,
                         "Flagset on inherited ACE are not correct")
        self.assertEqual(tfentry0.permset,
                         libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA,
                         "Permse of inherited ACE at index 0 are not correct")

    # Test INHERIT_ONLY flag with NO_PROPAGATE_INHERIT, ACE should be
    # inherited but inheritance flags should be removed
    def test_027_flagset_no_propagate_dir(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = libzfsacl.FLAG_DIRECTORY_INHERIT | \
            libzfsacl.FLAG_INHERIT_ONLY | libzfsacl.FLAG_NO_PROPAGATE_INHERIT
        newEntry.permset = libzfsacl.PERM_READ_DATA | \
            libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        os.makedirs(tddir)
        tddacl = libzfsacl.Acl(path=tddir)
        tdentry0 = tddacl.get_entry(0)
        shutil.rmtree(self.TDIR)
        self.assertEqual(tdentry0.idx, 0,
                         "Idx of inherited ACE at index 0 should be 0")
        self.assertEqual(tdentry0.entry_type, libzfsacl.ENTRY_TYPE_ALLOW,
                         "Inherited ACE at index 0 should be of type allow")
        self.assertEqual(tdentry0.who,
                         (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID),
                         "Inherited ACE who is not correct")
        self.assertEqual(tdentry0.flagset, libzfsacl.FLAG_INHERITED,
                         "Flagset on inherited ACE are not correct")
        self.assertEqual(tdentry0.permset,
                         libzfsacl.PERM_READ_DATA | libzfsacl.PERM_WRITE_DATA,
                         "Permse of inherited ACE at index 0 are not correct")

    # Following test cases verify that deny ACE permsets work correclty.
    # Prepend deny ACE denying that particular permission to the the ZFS
    # ACL user, then attempt to perform an action that should result in
    # failure.

    # Confirm deny ACE works for PERM_READ_DATA.
    def test_028_permset_deny_read_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_READ_DATA")

    # Test deny ACE works for PERM_WRITE_DATA
    def test_029_permset_deny_write_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        cmd = f"touch {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False,
                         "Failed to deny PERM_WRITE_DATA")

    # Test deny ACE works for PERM_EXECUTE
    def test_030_permset_deny_execute(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_EXECUTE
        tdacl.setacl(path=self.TDIR)
        cmd = f"cd {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        os.rmdir(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_EXECUTE")

    # Test deny ACE works for PERM_READ_ATTRIBUTES
    # PERM_READ_ATTRIBUTES is not implemented on Linux. It has no
    # equivalent in POSIX ACLs
    @unittest.skipIf(sys.platform == 'linux',
                     "PERM_READ_ATTRIBUTES is not supported for Linux")
    def test_031_permset_deny_read_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ATTRIBUTES
        tfacl.setacl(path=tfile)
        cmd = f"stat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False,
                         "Failed to deny PERM_READ_ATTRIBUTES")

    # Test deny ACE works for PERM_WRITE_ATTRIBUTES
    def test_032_permset_deny_write_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_ATTRIBUTES
        tfacl.setacl(path=tfile)
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False,
                         "Failed to deny PERM_WRITE_ATTRIBUTES")

    # Test deny ACE works for PERM_DELETE
    def test_033_permset_deny_delete(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_DELETE
        tfacl.setacl(path=tfile)
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_DELETE")

    # Test deny ACE works for PERM_DELETE_CHILD
    def test_034_permset_deny_delete_child(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        os.makedirs(tddir)
        tddacl = libzfsacl.Acl(path=tddir)
        newEntry = tddacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_DELETE_CHILD
        tddacl.setacl(path=tddir)
        cmd = f"rm -rf {tddir}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False,
                         "Failed to deny PERM_DELETE_CHILD")

    # Test deny ACE works for PERM_READ_ACL
    def test_035_permset_deny_read_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ACL
        tfacl.setacl(path=tfile)
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_READ_ACL")

    # Test deny ACE works for PERM_WRITE_ACL
    def test_036_permset_deny_write_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_ACL
        tfacl.setacl(path=tfile)
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_WRITE_ACL")

    # Test deny ACE works for PERM_WRITE_OWNER
    def test_037_permset_deny_write_owner(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_OWNER
        tfacl.setacl(path=tfile)
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False,
                         "Failed to deny PERM_WRITE_OWNER")

    # Test deny ACE works for PERM_ADD_FILE
    def test_038_permset_deny_add_file(self):
        tddir = f'{self.TDIR}/test_dir'
        tfile = f'{self.TDIR}/test_dir/test.txt'
        os.makedirs(self.TDIR)
        os.makedirs(tddir)
        tfacl = libzfsacl.Acl(path=tddir)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_DENY
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_ADD_FILE
        tfacl.setacl(path=tddir)
        cmd = f"touch {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False, "Failed to deny PERM_ADD_FILE")

    # Following test cases verify that allow ACE permsets work
    # correclty. Prepend allow ACE that allows a particular permission
    # to the ZFS ACL user, then attempt to perform an action that should
    # result in success.

    # Test allow ACE works for PERM_READ_DATA
    def test_039_permset_allow_read_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA | libzfsacl.PERM_EXECUTE
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True, "Failed to allow PERM_READ_DATA")

    # Test allow ACE works for PERM_WRITE_DATA
    def test_040_permset_allow_write_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_DATA
        tfacl.setacl(path=tfile)
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_WRITE_DATA")

    # Test allow ACE works for PERM_EXECUTE
    def test_041_permset_allow_execute(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_EXECUTE
        tdacl.setacl(path=self.TDIR)
        cmd = f"cd {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        os.rmdir(self.TDIR)
        self.assertEqual(res["result"], True, "Failed to allow PERM_EXECUTE")

    # Test allow ACE works for PERM_READ_ATTRIBUTES
    def test_042_permset_allow_read_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ATTRIBUTES | \
            libzfsacl.PERM_EXECUTE
        tfacl.setacl(path=tfile)
        cmd = f"stat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_READ_ATTRIBUTES")

    # Test allow ACE works for PERM_WRITE_ATTRIBUTES
    def test_043_permset_allow_write_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        tfacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_DATA | \
            libzfsacl.PERM_WRITE_ATTRIBUTES
        tfacl.setacl(path=self.TDIR)
        cmd = f"touch -a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_WRITE_ATTRIBUTES")

    # Test allow ACE works for PERM_DELETE
    def test_044_permset_allow_delete(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_DELETE | libzfsacl.PERM_EXECUTE | \
            libzfsacl.PERM_WRITE_DATA
        tdacl.setacl(path=self.TDIR)
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True, "Failed to allow PERM_DELETE")

    # Test allow ACE works for PERM_DELETE_CHILD
    def test_045_permset_allow_delete_child(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        os.makedirs(tddir)
        os.makedirs(f"{tddir}/tmp")
        tfacl = libzfsacl.Acl(path=tddir)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_DELETE_CHILD | \
            libzfsacl.PERM_EXECUTE | libzfsacl.PERM_WRITE_DATA
        tfacl.setacl(path=tddir)
        cmd = f"rm -rf {tddir}/tmp"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_DELETE_CHILD")

    # Test allow ACE works for PERM_READ_ACL
    def test_046_permset_allow_read_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ACL
        tfacl.setacl(path=tfile)
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True, "Failed to allow PERM_READ_ACL")

    # Test allow ACE works for PERM_WRITE_ACL
    def test_047_permset_allow_write_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_ACL
        tfacl.setacl(path=tfile)
        cmd = f"zfs_setnfs4facl -a u:{self.ZFS_ACL_STAFF1}:rw-pD-aARWcCos:" + \
            f"-------:allow {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_WRITE_ACL")

    # Test allow ACE works for PERM_WRITE_OWNER
    # PERM_WRITE_OWNER requires updates in Linux kernel, specifically in
    # setattr_prepare(), for permission check for chown and chgrp.
    # Without updates in Linux kernel to add permissions check,
    # PERM_WRITE_OWNER is not suported on Linux.
    @unittest.skipIf(sys.platform == 'linux',
                     "PERM_WRITE_OWNER is not supported for Linux")
    def test_048_permset_allow_write_owner(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_OWNER | \
            libzfsacl.PERM_EXECUTE | libzfsacl.PERM_WRITE_DATA | \
            libzfsacl.PERM_READ_ATTRIBUTES | libzfsacl.PERM_WRITE_ATTRIBUTES
        tfacl.setacl(path=tfile)
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True,
                         "Failed to allow PERM_WRITE_OWNER")

    # Test allow ACE works for PERM_ADD_FILE
    def test_049_permset_allow_add_file(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_ADD_FILE
        tdacl.setacl(path=self.TDIR)
        cmd = f"touch {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], True, "Failed to allow PERM_ADD_FILE")

    # Following test cases verify that allow ACE permsets don't work
    # without the specific flag set that is required to perform that
    # operation. Prepend allow ACE that allows all permissions, but the
    # one that is required to perform a particular operation. This
    # should result in failure.

    # Omit PERM_READ_DATA and test reading data
    def test_050_permset_omit_read_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_READ_DATA)
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Omit PERM_WRITE_DATA and test writing data
    def test_051_permset_omit_write_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_WRITE_DATA)
        tfacl.setacl(path=tfile)
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_EXECUTE
    def test_052_permset_omit_execute(self):
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl.delete_entry(2)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_EXECUTE)
        tdacl.setacl(path=self.TDIR)
        cmd = f"cd {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        os.rmdir(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_READ_ATTRIBUTES
    # PERM_READ_ATTRIBUTES is not implemented on Linux. It has no
    # equivalent in POSIX ACLs
    @unittest.skipIf(sys.platform == 'linux',
                     "PERM_READ_ATTRIBUTES is not implemented for Linux")
    def test_053_permset_omit_read_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & \
            ~(libzfsacl.PERM_READ_ATTRIBUTES)
        tfacl.setacl(path=tfile)
        cmd = f"stat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_WRITE_ATTRIBUTES
    def test_054_permset_omit_write_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & \
            ~(libzfsacl.PERM_WRITE_ATTRIBUTES | libzfsacl.PERM_EXECUTE |
              libzfsacl.PERM_WRITE_DATA)
        tfacl.setacl(path=tfile)
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_DELETE
    def test_055_permset_omit_delete(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & \
            ~(libzfsacl.PERM_DELETE | libzfsacl.PERM_DELETE_CHILD |
              libzfsacl.PERM_EXECUTE | libzfsacl.PERM_WRITE_DATA)
        tdacl.setacl(path=self.TDIR)
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_DELETE_CHILD
    def test_056_permset_omit_delete_child(self):
        tddir = f'{self.TDIR}/test_dir'
        os.makedirs(self.TDIR)
        os.makedirs(tddir)
        os.makedirs(f"{tddir}/tmp")
        tfacl = libzfsacl.Acl(path=tddir)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & \
            ~(libzfsacl.PERM_DELETE_CHILD | libzfsacl.PERM_EXECUTE |
              libzfsacl.PERM_WRITE_DATA)
        tfacl.setacl(path=tddir)
        cmd = f"rm -rf {tddir}/tmp"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_READ_ACL
    def test_057_permset_omit_read_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_READ_ACL)
        tfacl.setacl(path=tfile)
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_WRITE_ACL
    def test_058_permset_omit_write_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_WRITE_ACL)
        tfacl.setacl(path=tfile)
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_WRITE_OWNER
    def test_059_permset_omit_write_owner(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_WRITE_OWNER)
        tfacl.setacl(path=tfile)
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Test omit for PERM_ADD_FILE
    def test_060_permset_omit_add_file(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        tdacl = libzfsacl.Acl(path=self.TDIR)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = self.OMIT_PERMSET & ~(libzfsacl.PERM_ADD_FILE)
        tdacl.setacl(path=self.TDIR)
        cmd = f"touch {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        shutil.rmtree(self.TDIR)
        self.assertEqual(res["result"], False)

    # Following test cases verify that allow ACE permsets only allows a
    # user to perform that operation, and user does not have access to
    # other permissions. Add and ACE that allows the ZFS ACL user to
    # perform an operation, then perform other operations that are not
    # permitted to that user. This should result in failure.

    # User is allowed to stat on Linux since, PERM_READ_ATTRIBUTES is not
    # implemented on Linux.

    # Test allowing PERM_READ_DATA only allows reading data
    def test_061_permset_restrict_read_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_DATA
        tfacl.setacl(path=tfile)
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_DATA")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_READ_DATA")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_WRITE_DATA only allows writing data
    def test_062_permset_restrict_write_data(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w') as file:
            file.write("This is a test file.")
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_DATA
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_DATA")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_WRITE_DATA")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_EXECUTE only allows execution
    def test_063_permset_restrict_execute(self):
        os.makedirs(self.TDIR)
        tfile = f'{self.TDIR}/test.txt'
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl.delete_entry(2)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_EXECUTE
        tdacl.setacl(path=self.TDIR)
        cmd = f"ls {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f"zfs_getnfs4facl {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_EXECUTE")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_READ_ATTRIBUTES only allows to read attributes
    def test_064_permset_restrict_read_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ATTRIBUTES
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ATTRIBUTES")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_WRITE_ATTRIBUTES only allows to write attributes
    def test_065_permset_restrict_write_attrs(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_ATTRIBUTES
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ATTRIBUTES")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_WRITE_ATTRIBUTES")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_DELETE only allows to delete
    def test_066_permset_restrict_delete(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl.delete_entry(2)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_DELETE
        tdacl.setacl(path=self.TDIR)
        cmd = f"ls {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        cmd = f"zfs_getnfs4facl {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_DELETE")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_DELETE")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_READ_ACL only allows to read ACL
    def test_067_permset_restrict_read_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl.delete_entry(2)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_READ_ACL
        tdacl.setacl(path=self.TDIR)
        cmd = f"ls {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_READ_ACL")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_READ_ACL")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_WRITE_ACL only allows to write ACL
    def test_068_permset_restrict_write_acl(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tdacl = libzfsacl.Acl(path=self.TDIR)
        tdacl.delete_entry(2)
        newEntry = tdacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_ACL
        tdacl.setacl(path=self.TDIR)
        cmd = f"ls {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        cmd = f"chown {self.ZFS_ACL_STAFF1} {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        cmd = f"zfs_getnfs4facl {self.TDIR}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_ACL")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_WRITE_ACL")
        shutil.rmtree(self.TDIR)

    # Test allowing PERM_WRITE_OWNER only allows to write owner
    def test_069_permset_restrict_write_owner(self):
        tfile = f'{self.TDIR}/test.txt'
        os.makedirs(self.TDIR)
        with open(tfile, 'w'):
            pass
        tfacl = libzfsacl.Acl(path=tfile)
        tfacl.delete_entry(2)
        newEntry = tfacl.create_entry(0)
        newEntry.entry_type = libzfsacl.ENTRY_TYPE_ALLOW
        newEntry.who = (libzfsacl.WHOTYPE_USER, self.ZFS_ACL_STAFF1_UID)
        newEntry.flagset = 0
        newEntry.permset = libzfsacl.PERM_WRITE_OWNER
        tfacl.setacl(path=tfile)
        cmd = f"cat {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        cmd = f'echo -n "CAT" >> {tfile}'
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        cmd = f"touch a -m -t 201512180130.09 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        cmd = f"rm -f {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        cmd = f"zfs_getnfs4facl {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        cmd = f"zfs_setnfs4facl -x 0 {tfile}"
        res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
        self.assertEqual(res["result"], False,
                         "Failed to restrict PERM_WRITE_OWNER")
        if sys.platform != 'linux':
            cmd = f"stat {tfile}"
            res = run_as_user(cmd, self.ZFS_ACL_STAFF1)
            self.assertEqual(res["result"], False,
                             "Failed to restrict PERM_WRITE_OWNER")
        shutil.rmtree(self.TDIR)
