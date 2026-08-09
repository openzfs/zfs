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

verify_runnable "both"

del_user $PUSER
del_group $PGROUP
log_must add_group $PGROUP
log_must add_user $PGROUP $PUSER

#
# Verify the test user can execute the zfs utilities.  This may not
# be possible due to default permissions on the user home directory.
# This can be resolved granting group read access.
#
# chmod 0750 $HOME
#
user_run $PUSER zfs list ||
	log_unsupported "Test user $PUSER cannot execute zfs utilities"

DISK=${DISKS%% *}
default_setup_noexit $DISK

zfs set compression=off $TESTPOOL

log_pass
