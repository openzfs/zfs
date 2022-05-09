#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2022 by Christian Schwarz. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verifies that, with and only with the recv -c flag, we can
# receive a clone stream as an incremental stream into an existing
# dataset
#

verify_runnable "both"

log_onexit cleanup_pool $POOL2

log_must eval "zfs send $POOL@psnap | zfs recv $POOL2/recv@psnap"
log_mustnot eval "zfs send -i $POOL@psnap $POOL/pclone@init | zfs recv $POOL2/recv@init"
log_must eval "zfs send -i $POOL@psnap $POOL/pclone@init | zfs recv -C $POOL2/recv@init"

log_pass
