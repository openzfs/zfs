#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

################################################################################
#
# 'zfs destroy -t snapshot -r <snap>' should recursively destroy
# snapshots, and fail to recursively destroy filesystems
#
# 1. Create a hierarchy of filesystems, volumes, snapshots, and
# bookmarks:
#
#   tank
#   ├── #bookmark
#   ├── @snapshot
#   ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
#   ├── filesystem-with-snapshot
#   │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
#           └── @snapshot
#
# 2. Test recursively deleting objects of different types:
#    filesystems, volumes, snapshots, and bookmarks
# 3. Assert that the matching types are deleted and other types are
#    present
#
################################################################################

log_assert "'zfs destroy -t snapshot <snap>' only deletes snapshots"
log_onexit cleanup_testenv

destroyroot="$TESTPOOL/$TESTFS1"

function cleanup
{
	if datasetexists "$destroyroot"; then
		log_must zfs destroy -r "$destroyroot"
	fi
}

function setup_hierarchy
{
	cleanup

	#   tank
	log_must zfs create "$destroyroot"
	log_must zfs create "$destroyroot/tank"

	#   ├── #bookmark
	log_must zfs snapshot "$destroyroot/tank@bookmark"
	log_must zfs bookmark "$destroyroot/tank@bookmark" \
		 "$destroyroot/tank#bookmark"
	log_must zfs destroy "$destroyroot/tank@bookmark"

	#   ├── @snapshot
	log_must zfs snapshot "$destroyroot/tank@snapshot"

	#   ├── filesystem
	log_must zfs create "$destroyroot/tank/filesystem"

	#   ├── filesystem-with-bookmark
	#   │   └── #bookmark
	log_must zfs create "$destroyroot/tank/filesystem-with-bookmark"
	log_must zfs snapshot "$destroyroot/tank/filesystem-with-bookmark@bookmark"
	log_must zfs bookmark "$destroyroot/tank/filesystem-with-bookmark@bookmark" \
		 "$destroyroot/tank/filesystem-with-bookmark#bookmark"
	log_must zfs destroy "$destroyroot/tank/filesystem-with-bookmark@bookmark"

	#   ├── filesystem-with-snapshot
	#   │   └── @snapshot
	log_must zfs create "$destroyroot/tank/filesystem-with-snapshot"
	log_must zfs snapshot "$destroyroot/tank/filesystem-with-snapshot@snapshot"

	#   ├── filesystem-with-volume
	#   │    └── volume
	log_must zfs create "$destroyroot/tank/filesystem-with-volume"
	log_must zfs create -V 10M  "$destroyroot/tank/filesystem-with-volume/volume"

	#   └── filesystem-with-snapshotted-volume
	#       └── volume
	#           └── @snapshot
	log_must zfs create "$destroyroot/tank/filesystem-with-snapshotted-volume"
	log_must zfs create -V 10M  "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
	log_must zfs snapshot  "$destroyroot/tank/filesystem-with-snapshotted-volume/volume@snapshot"

}

# Verify boundaries of recursively deleting filesystems
#   tank
#   ├── #bookmark
#   ├── @snapshot
# x ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
#   ├── filesystem-with-snapshot
#   │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
#           └── @snapshot
setup_hierarchy
log_mustnot zfs destroy -t filesystem "$destroyroot/tank/filesystem-with-snapshot"
log_must zfs destroy -t filesystem -r "$destroyroot/tank/filesystem-with-volume"
log_must zfs destroy -t filesystem -r "$destroyroot/tank"
log_mustnot zfs get name "$destroyroot/tank/filesystem"

# Verify boundaries of recursively deleting volumes
#   tank
#   ├── #bookmark
#   ├── @snapshot
#   ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
#   ├── filesystem-with-snapshot
#   │   └── @snapshot
#   ├── filesystem-with-volume
# x │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
#           └── @snapshot
setup_hierarchy
log_mustnot zfs destroy -t volume -r "$destroyroot/tank/filesystem"
log_mustnot zfs destroy -t volume "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_must zfs destroy -t volume "$destroyroot/tank/filesystem-with-volume/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-volume/volume"

# Verify boundaries of recursively deleting snapshots
#   tank
#   ├── #bookmark
# x ├── @snapshot
#   ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
#   ├── filesystem-with-snapshot
# x │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
# x         └── @snapshot
setup_hierarchy
log_mustnot zfs destroy -t snapshot "$destroyroot/tank/filesystem"
log_mustnot zfs destroy -t snapshot "$destroyroot/tank/filesystem-with-volume/volume"
log_must zfs destroy -t snapshot -r "$destroyroot/tank@snapshot"

log_must zfs get name "$destroyroot/tank"
log_mustnot zfs get name "$destroyroot/tank@snapshot"

log_must zfs get name "$destroyroot/tank/filesystem-with-snapshot"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshot@snapshot"

log_must zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume@snapshot"


# Verify boundaries of recursively deleting bookmarks
#   tank
# x ├── #bookmark
#   ├── @snapshot
#   ├── filesystem
#   ├── filesystem-with-bookmark
# x │   └── #bookmark
#   ├── filesystem-with-snapshot
#   │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
#           └── @snapshot
setup_hierarchy
log_mustnot zfs destroy -t bookmark "$destroyroot/tank@snapshot"
log_mustnot zfs destroy -t bookmark "$destroyroot/tank/filesystem"
log_must zfs destroy -t bookmark "$destroyroot/tank#bookmark"
log_must zfs destroy -t bookmark "$destroyroot/tank/filesystem-with-bookmark#bookmark"

log_must zfs get name "$destroyroot/tank"
log_mustnot zfs get name "$destroyroot/tank#bookmark"

log_must zfs get name "$destroyroot/tank/filesystem-with-bookmark"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-bookmark#bookmark"

# Verify boundaries of recursively deleting all
# x tank
# x ├── #bookmark
# x ├── @snapshot
# x ├── filesystem
# x ├── filesystem-with-bookmark
# x │   └── #bookmark
# x ├── filesystem-with-snapshot
# x │   └── @snapshot
# x ├── filesystem-with-volume
# x │    └── volume
# x └── filesystem-with-snapshotted-volume
# x     └── volume
# x         └── @snapshot
setup_hierarchy
log_must zfs destroy -t all -r "$destroyroot/tank"
log_mustnot zfs get name "$destroyroot/tank"
log_mustnot zfs get name "$destroyroot/tank#bookmark"
log_mustnot zfs get name "$destroyroot/tank@snapshot"
log_mustnot zfs get name "$destroyroot/tank/filesystem"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-bookmark"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshot/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"

# Verify boundaries of recursively deleting volumes and snapshots
#   tank
#   ├── #bookmark
#   ├── @snapshot
#   ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
#   ├── filesystem-with-snapshot
#   │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
# x     └── volume
# x         └── @snapshot
setup_hierarchy
log_must zfs destroy -t volume,snapshot -r \
	 "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume@snapshot"

# Verify boundaries of recursively deleting filesystems and snapshots
#   tank
#   ├── #bookmark
# x ├── @snapshot
# x ├── filesystem
#   ├── filesystem-with-bookmark
#   │   └── #bookmark
# x ├── filesystem-with-snapshot
# x │   └── @snapshot
#   ├── filesystem-with-volume
#   │    └── volume
#   └── filesystem-with-snapshotted-volume
#       └── volume
# x         └── @snapshot
setup_hierarchy
log_must zfs destroy -t filesystem,snapshot -r "$destroyroot/tank"
log_must zfs get name "$destroyroot/tank"
log_must zfs get name "$destroyroot/tank#bookmark"
log_mustnot zfs get name "$destroyroot/tank@snapshot"
log_mustnot zfs get name "$destroyroot/tank/filesystem"
log_must zfs get name "$destroyroot/tank/filesystem-with-bookmark"
log_must zfs get name "$destroyroot/tank/filesystem-with-bookmark#bookmark"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshot/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_must zfs get name "$destroyroot/tank/filesystem-with-volume"
log_must zfs get name "$destroyroot/tank/filesystem-with-volume/volume"
log_must zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume"
log_must zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume"
log_mustnot zfs get name "$destroyroot/tank/filesystem-with-snapshotted-volume/volume@snapshot"


log_pass "'zfs destroy -t snapshot <snap>' only deletes snapshots"
