#!/bin/sh
# SPDX-License-Identifier: CDDL-1.0

autoreconf -fiv "$(dirname "$0")" && rm -rf "$(dirname "$0")"/autom4te.cache
