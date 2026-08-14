#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# NFSv4 ACLs: VOP_GETACL/SETACL/ACLCHECK with ACL_TYPE_NFS4 when
# acltype=nfsv4 (FreeBSD zfs_freebsd_getacl path; default on new
# NetBSD/FreeBSD pools).
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_posix_acl_utils

cleanup_user_group

log_must add_group $ZFS_ACL_STAFF_GROUP
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF1

DISK=${DISKS%% *}
default_setup_noexit $DISK
log_must chmod 777 $TESTDIR

# New pools default to nfsv4 like FreeBSD.  Linux posix datasets
# keep acltype=posix on import; this fs was just created here.
typeset def
def=$(zfs get -H -o value acltype $TESTPOOL/$TESTFS)
if [ "$def" != "nfsv4" ]; then
	log_fail "new filesystem acltype is '$def', expected nfsv4"
fi

log_must zfs set acltype=nfsv4 $TESTPOOL/$TESTFS
log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS
log_must zfs set aclinherit=passthrough $TESTPOOL/$TESTFS

log_pass
