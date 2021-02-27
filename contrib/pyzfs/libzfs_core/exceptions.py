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
Exceptions that can be raised by libzfs_core operations.
"""
from __future__ import absolute_import, division, print_function

import errno
from ._constants import (
    ECHRNG,
    ECKSUM,
    ETIME,
    ZFS_ERR_CHECKPOINT_EXISTS,
    ZFS_ERR_DISCARDING_CHECKPOINT,
    ZFS_ERR_NO_CHECKPOINT,
    ZFS_ERR_DEVRM_IN_PROGRESS,
    ZFS_ERR_VDEV_TOO_BIG,
    ZFS_ERR_WRONG_PARENT,
    ZFS_ERR_RAIDZ_EXPAND_IN_PROGRESS,
    zfs_errno
)


class ZFSError(Exception):
    errno = None
    message = None
    name = None

    def __str__(self):
        if self.name is not None:
            return "[Errno %d] %s: '%s'" % (
                self.errno, self.message, self.name)
        else:
            return "[Errno %d] %s" % (self.errno, self.message)

    def __repr__(self):
        return "%s(%r, %r)" % (
            self.__class__.__name__, self.errno, self.message)


class ZFSGenericError(ZFSError):

    def __init__(self, errno, name, message):
        self.errno = errno
        self.message = message
        self.name = name


class ZFSInitializationFailed(ZFSError):
    message = "Failed to initialize libzfs_core"

    def __init__(self, errno):
        self.errno = errno


class MultipleOperationsFailure(ZFSError):

    def __init__(self, errors, suppressed_count):
        # Use first of the individual error codes
        # as an overall error code.  This is more consistent.
        self.errno = errors[0].errno
        self.errors = errors
        # this many errors were encountered but not placed on the `errors` list
        self.suppressed_count = suppressed_count

    def __str__(self):
        return "%s, %d errors included, %d suppressed" % (
            ZFSError.__str__(self), len(self.errors), self.suppressed_count)

    def __repr__(self):
        return "%s(%r, %r, errors=%r, suppressed=%r)" % (
            self.__class__.__name__, self.errno, self.message, self.errors,
            self.suppressed_count)


class DatasetNotFound(ZFSError):

    """
    This exception is raised when an operation failure can be caused by a
    missing snapshot or a missing filesystem and it is impossible to
    distinguish between the causes.
    """
    errno = errno.ENOENT
    message = "Dataset not found"

    def __init__(self, name):
        self.name = name


class DatasetExists(ZFSError):

    """
    This exception is raised when an operation failure can be caused by an
    existing snapshot or filesystem and it is impossible to distinguish between
    the causes.
    """
    errno = errno.EEXIST
    message = "Dataset already exists"

    def __init__(self, name):
        self.name = name


class NotClone(ZFSError):
    errno = errno.EINVAL
    message = "Filesystem is not a clone, can not promote"

    def __init__(self, name):
        self.name = name


class FilesystemExists(DatasetExists):
    message = "Filesystem already exists"

    def __init__(self, name):
        self.name = name


class FilesystemNotFound(DatasetNotFound):
    message = "Filesystem not found"

    def __init__(self, name):
        self.name = name


class ParentNotFound(ZFSError):
    errno = errno.ENOENT
    message = "Parent not found"

    def __init__(self, name):
        self.name = name


class WrongParent(ZFSError):
    errno = ZFS_ERR_WRONG_PARENT
    message = "Parent dataset is not a filesystem"

    def __init__(self, name):
        self.name = name


class SnapshotExists(DatasetExists):
    message = "Snapshot already exists"

    def __init__(self, name):
        self.name = name


class SnapshotNotFound(DatasetNotFound):
    message = "Snapshot not found"

    def __init__(self, name):
        self.name = name


class SnapshotNotLatest(ZFSError):
    errno = errno.EEXIST
    message = "Snapshot is not the latest"

    def __init__(self, name):
        self.name = name


class SnapshotIsCloned(ZFSError):
    errno = errno.EEXIST
    message = "Snapshot is cloned"

    def __init__(self, name):
        self.name = name


class SnapshotIsHeld(ZFSError):
    errno = errno.EBUSY
    message = "Snapshot is held"

    def __init__(self, name):
        self.name = name


class DuplicateSnapshots(ZFSError):
    errno = errno.EXDEV
    message = "Requested multiple snapshots of the same filesystem"

    def __init__(self, name):
        self.name = name


class SnapshotFailure(MultipleOperationsFailure):
    message = "Creation of snapshot(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(SnapshotFailure, self).__init__(errors, suppressed_count)


class SnapshotDestructionFailure(MultipleOperationsFailure):
    message = "Destruction of snapshot(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(SnapshotDestructionFailure, self).__init__(
            errors, suppressed_count)


class BookmarkExists(ZFSError):
    errno = errno.EEXIST
    message = "Bookmark already exists"

    def __init__(self, name):
        self.name = name


class BookmarkNotFound(ZFSError):
    errno = errno.ENOENT
    message = "Bookmark not found"

    def __init__(self, name):
        self.name = name


class BookmarkMismatch(ZFSError):
    errno = errno.EINVAL
    message = "source is not an ancestor of the new bookmark's dataset"

    def __init__(self, name):
        self.name = name


class BookmarkSourceInvalid(ZFSError):
    errno = errno.EINVAL
    message = "Bookmark source is not a valid snapshot or existing bookmark"

    def __init__(self, name):
        self.name = name


class BookmarkNotSupported(ZFSError):
    errno = errno.ENOTSUP
    message = "Bookmark feature is not supported"

    def __init__(self, name):
        self.name = name


class BookmarkFailure(MultipleOperationsFailure):
    message = "Creation of bookmark(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(BookmarkFailure, self).__init__(errors, suppressed_count)


class BookmarkDestructionFailure(MultipleOperationsFailure):
    message = "Destruction of bookmark(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(BookmarkDestructionFailure, self).__init__(
            errors, suppressed_count)


class BadHoldCleanupFD(ZFSError):
    errno = errno.EBADF
    message = "Bad file descriptor as cleanup file descriptor"


class HoldExists(ZFSError):
    errno = errno.EEXIST
    message = "Hold with a given tag already exists on snapshot"

    def __init__(self, name):
        self.name = name


class HoldNotFound(ZFSError):
    errno = errno.ENOENT
    message = "Hold with a given tag does not exist on snapshot"

    def __init__(self, name):
        self.name = name


class HoldFailure(MultipleOperationsFailure):
    message = "Placement of hold(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(HoldFailure, self).__init__(errors, suppressed_count)


class HoldReleaseFailure(MultipleOperationsFailure):
    message = "Release of hold(s) failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(HoldReleaseFailure, self).__init__(errors, suppressed_count)


class SnapshotMismatch(ZFSError):
    errno = errno.ENODEV
    message = "Snapshot is not descendant of source snapshot"

    def __init__(self, name):
        self.name = name


class StreamMismatch(ZFSError):
    errno = errno.ENODEV
    message = "Stream is not applicable to destination dataset"

    def __init__(self, name):
        self.name = name


class DestinationModified(ZFSError):
    errno = errno.ETXTBSY
    message = "Destination dataset has modifications that can not be undone"

    def __init__(self, name):
        self.name = name


class BadStream(ZFSError):
    errno = ECKSUM
    message = "Bad backup stream"


class StreamFeatureNotSupported(ZFSError):
    errno = errno.ENOTSUP
    message = "Stream contains unsupported feature"


class UnknownStreamFeature(ZFSError):
    errno = errno.ENOTSUP
    message = "Unknown feature requested for stream"


class StreamFeatureInvalid(ZFSError):
    errno = errno.EINVAL
    message = "Kernel modules must be upgraded to receive this stream"


class StreamFeatureIncompatible(ZFSError):
    errno = errno.EINVAL
    message = "Incompatible embedded feature with encrypted receive"


class StreamTruncated(ZFSError):
    errno = zfs_errno.ZFS_ERR_STREAM_TRUNCATED
    message = "incomplete stream"


class ReceivePropertyFailure(MultipleOperationsFailure):
    message = "Receiving of properties failed for one or more reasons"

    def __init__(self, errors, suppressed_count):
        super(ReceivePropertyFailure, self).__init__(errors, suppressed_count)


class StreamIOError(ZFSError):
    message = "I/O error while writing or reading stream"

    def __init__(self, errno):
        self.errno = errno


class ZIOError(ZFSError):
    errno = errno.EIO
    message = "I/O error"

    def __init__(self, name):
        self.name = name


class NoSpace(ZFSError):
    errno = errno.ENOSPC
    message = "No space left"

    def __init__(self, name):
        self.name = name


class QuotaExceeded(ZFSError):
    errno = errno.EDQUOT
    message = "Quota exceeded"

    def __init__(self, name):
        self.name = name


class DatasetBusy(ZFSError):
    errno = errno.EBUSY
    message = "Dataset is busy"

    def __init__(self, name):
        self.name = name


class NameTooLong(ZFSError):
    errno = errno.ENAMETOOLONG
    message = "Dataset name is too long"

    def __init__(self, name):
        self.name = name


class NameInvalid(ZFSError):
    errno = errno.EINVAL
    message = "Invalid name"

    def __init__(self, name):
        self.name = name


class SnapshotNameInvalid(NameInvalid):
    message = "Invalid name for snapshot"

    def __init__(self, name):
        self.name = name


class FilesystemNameInvalid(NameInvalid):
    message = "Invalid name for filesystem or volume"

    def __init__(self, name):
        self.name = name


class BookmarkNameInvalid(NameInvalid):
    message = "Invalid name for bookmark"

    def __init__(self, name):
        self.name = name


class ReadOnlyPool(ZFSError):
    errno = errno.EROFS
    message = "Pool is read-only"

    def __init__(self, name):
        self.name = name


class SuspendedPool(ZFSError):
    errno = errno.EAGAIN
    message = "Pool is suspended"

    def __init__(self, name):
        self.name = name


class PoolNotFound(ZFSError):
    errno = errno.EXDEV
    message = "No such pool"

    def __init__(self, name):
        self.name = name


class PoolsDiffer(ZFSError):
    errno = errno.EXDEV
    message = "Source and target belong to different pools"

    def __init__(self, name):
        self.name = name


class FeatureNotSupported(ZFSError):
    errno = errno.ENOTSUP
    message = "Feature is not supported in this version"

    def __init__(self, name):
        self.name = name


class PropertyNotSupported(ZFSError):
    errno = errno.ENOTSUP
    message = "Property is not supported in this version"

    def __init__(self, name):
        self.name = name


class PropertyInvalid(ZFSError):
    errno = errno.EINVAL
    message = "Invalid property or property value"

    def __init__(self, name):
        self.name = name


class DatasetTypeInvalid(ZFSError):
    errno = errno.EINVAL
    message = "Specified dataset type is unknown"

    def __init__(self, name):
        self.name = name


class UnknownCryptCommand(ZFSError):
    errno = errno.EINVAL
    message = "Specified crypt command is invalid"

    def __init__(self, name):
        self.name = name


class EncryptionKeyNotLoaded(ZFSError):
    errno = errno.EACCES
    message = "Encryption key is not currently loaded"


class EncryptionKeyAlreadyLoaded(ZFSError):
    errno = errno.EEXIST
    message = "Encryption key is already loaded"


class EncryptionKeyInvalid(ZFSError):
    errno = errno.EACCES
    message = "Incorrect encryption key provided"


class ZCPError(ZFSError):
    errno = None
    message = None


class ZCPSyntaxError(ZCPError):
    errno = errno.EINVAL
    message = "Channel program contains syntax errors"

    def __init__(self, details):
        self.details = details


class ZCPRuntimeError(ZCPError):
    errno = ECHRNG
    message = "Channel programs encountered a runtime error"

    def __init__(self, details):
        self.details = details


class ZCPLimitInvalid(ZCPError):
    errno = errno.EINVAL
    message = "Channel program called with invalid limits"


class ZCPTimeout(ZCPError):
    errno = ETIME
    message = "Channel program timed out"


class ZCPSpaceError(ZCPError):
    errno = errno.ENOSPC
    message = "Channel program exhausted the memory limit"


class ZCPMemoryError(ZCPError):
    errno = errno.ENOMEM
    message = "Channel program return value too large"


class ZCPPermissionError(ZCPError):
    errno = errno.EPERM
    message = "Channel programs must be run as root"


class CheckpointExists(ZFSError):
    errno = ZFS_ERR_CHECKPOINT_EXISTS
    message = "Pool already has a checkpoint"


class CheckpointNotFound(ZFSError):
    errno = ZFS_ERR_NO_CHECKPOINT
    message = "Pool does not have a checkpoint"


class CheckpointDiscarding(ZFSError):
    errno = ZFS_ERR_DISCARDING_CHECKPOINT
    message = "Pool checkpoint is being discarded"


class DeviceRemovalRunning(ZFSError):
    errno = ZFS_ERR_DEVRM_IN_PROGRESS
    message = "A vdev is currently being removed"


class DeviceTooBig(ZFSError):
    errno = ZFS_ERR_VDEV_TOO_BIG
    message = "One or more top-level vdevs exceed the maximum vdev size"


class RaidzExpansionRunning(ZFSError):
    errno = ZFS_ERR_RAIDZ_EXPAND_IN_PROGRESS
    message = "A raidz device is currently expanding"


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
