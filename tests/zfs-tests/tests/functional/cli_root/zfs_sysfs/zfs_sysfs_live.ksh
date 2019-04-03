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
# Copyright (c) 2018, 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test if the expected '/sys/module/zfs/<dir>/<attr>' are present
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "sysfs is linux-only"
fi

claim="Expected '/sys/module/zfs/<dir>/<attr>' attributes are present"

kernel_feature_attr="/sys/module/zfs/features.kernel/org.zfsonlinux:vdev_trim/supported"
pool_feature_attr="/sys/module/zfs/features.pool/org.open-zfs:large_blocks/guid"
pool_prop__attr="/sys/module/zfs/properties.pool/comment/values"
ds_prop__attr="/sys/module/zfs/properties.dataset/recordsize/values"

log_assert $claim

log_must cat $kernel_feature_attr
log_must cat $pool_feature_attr
log_must cat $pool_prop__attr
log_must cat $ds_prop__attr

# force a read of all the attributes for show func code coverage
log_must grep -R "[a-z]" /sys/module/zfs/features.*
log_must grep -R "[a-z]" /sys/module/zfs/properties.*
log_mustnot grep -RE "[^[:print:]]" /sys/module/zfs/properties.*

log_pass $claim
