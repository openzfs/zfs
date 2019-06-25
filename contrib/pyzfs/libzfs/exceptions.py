#
# Copyright 2019 Hudson River Trading LLC.
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

import errno


class ZFSError(Exception):
    errno = None
    message = None
    name = None

    def __str__(self):
        if self.name is not None:
            return "[Errno %d] %s: '%s'" % (self.errno,
                                            self.message,
                                            self.name)
        else:
            return "[Errno %d] %s" % (self.errno, self.message)

    def __repr__(self):
        return "%s(%r, %r)" % (
            self.__class__.__name__,
            self.errno,
            self.message,
        )


class LibZfsInitError(ZFSError):
    """
    This error is usually raised if the ZFS driver is not found.
    Usually, this occurs either because the ZFS modules are not installed.
    """

    errno = -1
    message = "Failed to open a handle to the ZFS driver"


class ZpoolOpenError(ZFSError):
    errno = -1
    message = "Failed to open zpool"

    def __init__(self, message):
        self.message += " " + message


class ZpoolConfigFeatureFetchError(ZFSError):
    errno = -1
    message = "Failed to fetch config features for the zpool"

    def __init__(self, message=None):
        if message is not None:
            self.message = message


class ZpoolPropertyFetchError(ZFSError):
    errno = -1
    message = "Failed to fetch property"

    def __init__(self, message):
        self.message += " " + message


class ZpoolIterError(ZFSError):
    errno = -1
    message = "Failed to iterate over Zpools"


class ZfsDatasetIterError(ZFSError):
    errno = -1
    message = "Failed to iterate over ZFS datasets"


class ZfsDatasetUserspaceError(ZFSError):
    errno = errno.ENOTSUP
    message = "Failed to fetch userspace property"

    def __init__(self, message):
        self.message += " " + message


class ZfsDatasetOpenError(ZFSError):
    errno = -1
    message = "Failed to open zfs dataset"

    def __init__(self, message):
        self.message += " " + message


class ZfsPropertyFetchError(ZFSError):
    errno = -1
    message = "Failed to fetch property"

    def __init__(self, message):
        self.message += " " + message


class InvalidValuesError(ZFSError):
    errno = errno.EINVAL
    message = "Invalid values passed to the function"


class FeatureNotSupportedError(ZFSError):
    errno = errno.ENOTSUP
    message = "This feature is not supported on this platform"


class NoEntryFoundError(ZFSError):
    errno = errno.ENOENT
    message = "No corresponding entry found"


class ZpoolHistoryFetchError(ZFSError):
    errno = errno.EFAULT
    # Asking the user to run the command to find the reason
    # why the history was not fetched is due to
    # the fact that the zpool_get_history(...) command
    # does not actually return the error code corresponding
    # to a problem; instead if ALWAYS returns -1. Hence, this workaround.
    message = "Could not fetch zpool history." + \
        "Please use libzfs_error_description(<libzfs_handle>) to know why"


class InsufficientPerms(ZFSError):
    errno = errno.EPERM
    message = "The function you are trying to call requires root permissions"

    def __init__(self, message):
        self.message += " " + message
