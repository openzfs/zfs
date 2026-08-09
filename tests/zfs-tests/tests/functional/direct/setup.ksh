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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
verify_runnable "global"

if tunable_exists DIO_ENABLED ; then
	log_must save_tunable DIO_ENABLED
	log_must set_tunable32 DIO_ENABLED 1
fi

default_raidz_setup_noexit "$DISKS"
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_pass
