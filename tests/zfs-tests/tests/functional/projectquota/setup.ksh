#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
