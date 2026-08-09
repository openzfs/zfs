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
