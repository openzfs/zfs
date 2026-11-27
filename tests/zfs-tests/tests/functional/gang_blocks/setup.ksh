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
# Copyright (c) 2025 by Klara Inc.
#

. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

save_tunable METASLAB_FORCE_GANGING
save_tunable METASLAB_FORCE_GANGING_PCT
set_tunable64 METASLAB_FORCE_GANGING $FORCE_GANGING
set_tunable32 METASLAB_FORCE_GANGING_PCT $FORCE_GANGING_PCT
