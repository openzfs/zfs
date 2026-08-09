#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check the invalid parameter of zfs set defaultproject{obj}quota
#
#
# STRATEGY:
#	1. check the invalid zfs set defaultproject{obj}quota to fs
#	2. check the valid zfs set defaultproject{obj}quota to snapshots
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs set defaultproject{obj}quota"
typeset snap_fs=$QFS@snap

log_must zfs snapshot $snap_fs

log_note "can set all numeric id even if that id does not exist"
log_must zfs set defaultprojectquota=100m $QFS

set -A sizes "100mfsd" "m0.12m" "GGM" "-1234-m" "123m-m"
for size in "${sizes[@]}"; do
	log_note "can not set defaultprojectquota with invalid size parameter"
	log_mustnot zfs set defaultprojectquota=$size $QFS
done

log_note "can not set defaultprojectquota to snapshot $snap_fs"
log_mustnot zfs set defaultprojectquota=100m $snap_fs

log_note "can not set defaultprojectobjquota with invalid size parameter"
log_mustnot zfs set defaultprojectobjquota=100msfsd $QFS

log_note "can not set defaultprojectobjquota to snapshot $snap_fs"
log_mustnot zfs set defaultprojectobjquota=100m $snap_fs

log_pass "Check the invalid parameter of zfs set defaultproject{obj}quota"
