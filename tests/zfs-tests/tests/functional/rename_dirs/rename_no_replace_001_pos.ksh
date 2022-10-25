#!/bin/ksh -p
# SPDX-License-Identifier: 0BSD

. $STF_SUITE/include/libtest.shlib

is_linux || [ $(linux_version) -ge $(linux_version "3.15.0") ] || log_unsupported "renameat2(2) is Linux-only"

verify_runnable "both"

log_assert "renameat2(RENAME_NOREPLACE) works"
log_onexit log_must rm -r "$TESTDIR"/*

mkdir "$TESTDIR"/{a,b,c}

log_must rename_no_replace "$TESTDIR" "$TESTDIR"  ; rm "$TESTDIR"/from
log_must rename_no_replace "$TESTDIR" "$TESTDIR"/a

cd "$TESTDIR"/b
log_must rename_no_replace ../c; rm to
log_must rename_no_replace

log_pass
