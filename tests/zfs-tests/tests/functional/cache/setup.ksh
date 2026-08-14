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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

verify_runnable "global"

# Persistent QEMU disks keep labels across ZTS runs.
for d in $DISKS $LDEV $LDEV2; do
	[ -n "$d" ] || continue
	zpool labelclear -f "$d" >/dev/null 2>&1 || true
	zpool labelclear -f $(to_usable_path $d) >/dev/null 2>&1 || true
done

log_must rm -rf $VDIR $VDIR2
log_must mkdir -p $VDIR $VDIR2
log_must mkfile $SIZE $VDEV $VDEV2
for d in $LDEV $LDEV2; do
	[ -n "$d" ] || continue
	# Real disks already exist; only create file stand-ins.
	up=$(to_usable_path "$d")
	if [ -b "$d" ] || [ -c "$d" ] || [ -b "$up" ] || [ -c "$up" ]; then
		continue
	fi
	case " $VDEV $VDEV2 " in
	*" $d "*) continue ;;
	esac
	log_must mkdir -p "$(dirname "$d")"
	[ -f "$d" ] || log_must mkfile $SIZE "$d"
done

log_pass
