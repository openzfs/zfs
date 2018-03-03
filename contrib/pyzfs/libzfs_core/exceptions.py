# Copyright 2015 ClusterHQ. See LICENSE file for details.

"""
Exceptions that can be raised by libzfs_core operations.
"""

import errno


class ZFSError(Exception):
    errno = None
    message = None
    name = None

    def __str__(self):
        if self.name is not None:
            return "[Errno %d] %s: '%s'" % (self.errno, self.message, self.name)
        else:
            return "[Errno %d] %s" % (self.errno, self.message)

    def __repr__(self):
        return "%s(%r, %r)" % (self.__class__.__name__, self.errno, self.message)


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
        #: this many errors were encountered but not placed on the `errors` list
        self.suppressed_count = suppressed_count

    def __str__(self):
        return "%s, %d errors included, %d suppressed" % (ZFSError.__str__(self),
                                                          len(self.errors), self.suppressed_count)

    def __repr__(self):
        return "%s(%r, %r, errors=%r, supressed=%r)" % (self.__class__.__name__,
                                                        self.errno, self.message, self.errors, self.suppressed_count)


class DatasetNotFound(ZFSError):

    """
    This exception is raised when an operation failure can be caused by a missing
    snapshot or a missing filesystem and it is impossible to distinguish between
    the causes.
    """
    errno = errno.ENOENT
    message = "Dataset not found"

    def __init__(self, name):
        self.name = name


class DatasetExists(ZFSError):

    """
    This exception is raised when an operation failure can be caused by an existing
    snapshot or filesystem and it is impossible to distinguish between
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
    errno = errno.EINVAL
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
        super(SnapshotDestructionFailure, self).__init__(errors, suppressed_count)


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
    message = "Bookmark is not in snapshot's filesystem"

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
        super(BookmarkDestructionFailure, self).__init__(errors, suppressed_count)


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
    errno = errno.EINVAL
    message = "Bad backup stream"


class StreamFeatureNotSupported(ZFSError):
    errno = errno.ENOTSUP
    message = "Stream contains unsupported feature"


class UnknownStreamFeature(ZFSError):
    errno = errno.ENOTSUP
    message = "Unknown feature requested for stream"


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
    message = "Quouta exceeded"

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


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
