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
Important `libzfs_core` constants.
"""

from __future__ import absolute_import, division, print_function


# https://stackoverflow.com/a/1695250
def enum(*sequential, **named):
    enums = dict(((b, a) for a, b in enumerate(sequential)), **named)
    return type('Enum', (), enums)


#: Maximum length of any ZFS name.
MAXNAMELEN = 255
#: Default channel program limits
ZCP_DEFAULT_INSTRLIMIT = 10 * 1000 * 1000
ZCP_DEFAULT_MEMLIMIT = 10 * 1024 * 1024
#: Encryption wrapping key length
WRAPPING_KEY_LEN = 32
#: Encryption key location enum
zfs_key_location = enum(
    'ZFS_KEYLOCATION_NONE',
    'ZFS_KEYLOCATION_PROMPT',
    'ZFS_KEYLOCATION_URI'
)
#: Encryption key format enum
zfs_keyformat = enum(
    'ZFS_KEYFORMAT_NONE',
    'ZFS_KEYFORMAT_RAW',
    'ZFS_KEYFORMAT_HEX',
    'ZFS_KEYFORMAT_PASSPHRASE'
)
# Encryption algorithms enum
zio_encrypt = enum(
    'ZIO_CRYPT_INHERIT',
    'ZIO_CRYPT_ON',
    'ZIO_CRYPT_OFF',
    'ZIO_CRYPT_AES_128_CCM',
    'ZIO_CRYPT_AES_192_CCM',
    'ZIO_CRYPT_AES_256_CCM',
    'ZIO_CRYPT_AES_128_GCM',
    'ZIO_CRYPT_AES_192_GCM',
    'ZIO_CRYPT_AES_256_GCM'
)
# ZFS-specific error codes
ZFS_ERR_CHECKPOINT_EXISTS = 1024
ZFS_ERR_DISCARDING_CHECKPOINT = 1025
ZFS_ERR_NO_CHECKPOINT = 1026
ZFS_ERR_DEVRM_IN_PROGRESS = 1027
ZFS_ERR_VDEV_TOO_BIG = 1028
ZFS_ERR_WRONG_PARENT = 1033


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
