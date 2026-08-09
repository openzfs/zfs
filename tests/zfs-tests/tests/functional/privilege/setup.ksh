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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

ZFS_USER=zfsrbac
USES_NIS=false

# if we're running NIS, turn it off until we clean up
# (it can cause useradd to take a long time, hitting our TIMEOUT)
if svcs svc:/network/nis/client:default | grep -q online
then
  svcadm disable svc:/network/nis/client:default
  USES_NIS=true
fi


# create a unique user that we can use to run the tests,
# making sure not to clobber any existing users.
FOUND=""
while [ -z "${FOUND}" ]
do
  USER_EXISTS=$( grep $ZFS_USER /etc/passwd )
  if [ ! -z "${USER_EXISTS}" ]
  then
      ZFS_USER="${ZFS_USER}x"
  else
      FOUND="true"
  fi
done

log_must mkdir -p /export/home/$ZFS_USER
log_must useradd -c "ZFS Privileges Test User" -d /export/home/$ZFS_USER $ZFS_USER

echo $ZFS_USER > $TEST_BASE_DIR/zfs-privs-test-user.txt
echo $USES_NIS > $TEST_BASE_DIR/zfs-privs-test-nis.txt

log_pass
