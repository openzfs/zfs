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

#
# Copyright (c) 2021 by Christian Schwarz. All rights reserved.
#

include $(top_srcdir)/config/Rules.am

# the stdout of this target is fed to bindgen's clang_args() call
print_clang_args_for_bindgen:
	@echo CLANG_ARGS_FOR_BINDGEN $(DEFAULT_INCLUDES) $(AM_CFLAGS) $(AM_CPPFLAGS)
