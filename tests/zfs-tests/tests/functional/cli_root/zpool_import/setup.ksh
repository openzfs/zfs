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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

verify_runnable "global"

create_pool "$TESTPOOL" "$DISK"

if [[ -d $TESTDIR ]]; then
	rm -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
	mkdir -p $TESTDIR || log_unresolved Could not create $TESTDIR
fi

log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

[[ ! -d $DEVICE_DIR ]] && \
	log_must mkdir -p $DEVICE_DIR

i=0
while (( i < $MAX_NUM )); do
	log_must truncate -s $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
	(( i = i + 1 ))
done

log_pass
