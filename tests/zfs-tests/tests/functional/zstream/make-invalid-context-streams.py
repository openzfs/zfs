#!/usr/bin/env python3
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
# Copyright (c) 2026 by Matthias Goergens. All rights reserved.
#

import struct
import sys
from pathlib import Path

DRR_BEGIN = 0
DRR_OBJECT = 1
DRR_END = 5
DMU_BACKUP_MAGIC = 0x2F5BACBAC
DMU_SUBSTREAM_VERSIONINFO = 0x18C0011
DMU_COMPOUNDSTREAM_VERSIONINFO = (
    (DMU_SUBSTREAM_VERSIONINFO & ~0x3) | 0x2
)
BEGIN_PAYLOAD_MAX = 1 << 28
RECORD_SIZE = 312


def record(byte_order, record_type, payload_size=0):
    header = struct.pack(f"{byte_order}II", record_type, payload_size)
    return header + bytes(RECORD_SIZE - len(header))


def begin(byte_order, payload_size=0, compound=False):
    toname = b"test/source@baseline".ljust(256, b"\0")
    value = struct.pack(
        f"{byte_order}IIQQQIIQQ256s",
        DRR_BEGIN,
        payload_size,
        DMU_BACKUP_MAGIC,
        (DMU_COMPOUNDSTREAM_VERSIONINFO if compound
         else DMU_SUBSTREAM_VERSIONINFO),
        0,
        2,
        0,
        1,
        0,
        toname,
    )
    assert len(value) == RECORD_SIZE
    return value


output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
for byte_order, name in ((">", "big"), ("<", "little")):
    begin_record = begin(byte_order)
    end_record = record(byte_order, DRR_END)
    (output / f"nested-begin-{name}.zsend").write_bytes(
        begin_record + begin_record
    )
    (output / f"record-after-end-{name}.zsend").write_bytes(
        begin_record + end_record + record(byte_order, DRR_OBJECT)
    )
    (output / f"extra-end-{name}.zsend").write_bytes(
        begin_record + end_record + end_record
    )
    (output / f"compound-extra-end-{name}.zsend").write_bytes(
        begin(byte_order, compound=True) + end_record + end_record + end_record
    )
    (output / f"oversized-begin-{name}.zsend").write_bytes(
        begin(byte_order, BEGIN_PAYLOAD_MAX + 1)
    )
