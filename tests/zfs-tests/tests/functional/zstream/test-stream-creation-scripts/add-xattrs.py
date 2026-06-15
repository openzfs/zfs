#!/tmp/zstream-venv/bin/python3
"""Add at least 1024 bytes of random extended attributes to files."""

#
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the Common
# Development and Distribution License ("CDDL"), version 1.0. You may only use
# this file in accordance with the terms of version 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this source. A
# copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

import argparse
import os
import random
import sys
from lorem_text import lorem

ADJECTIVES = [
    "boogie", "funky", "wobbly", "snazzy", "jazzy", "groovy", "zippy",
    "bouncy", "fluffy", "crunchy", "sparkly", "fuzzy", "spiffy", "dandy",
    "peppy", "snappy", "sassy", "zesty", "swanky", "nifty", "plucky",
    "quirky", "wacky", "goofy", "dizzy", "breezy", "cheery", "perky",
    "frisky", "chirpy", "feisty", "jolly", "lively", "merry", "spunky",
    "zippy", "vivid", "brisk", "sunny", "witty", "kinky",
]

NOUNS = [
    "woogie", "monkey", "noodle", "pickle", "muffin", "waffle", "pebble",
    "wobble", "doodle", "tangle", "giggle", "wiggle", "jiggle", "sparkle",
    "crinkle", "twinkle", "frizzle", "drizzle", "sizzle", "fizzle",
    "puddle", "bubble", "muddle", "huddle", "cuddle", "juggle", "muggle",
    "snuggle", "tuggle", "buggle", "nugget", "widget", "gadget", "gibbet",
    "trinket", "bracket", "racket", "jacket", "ticket", "cricket", "thicket",
    "biscuit", "circuit", "summit", "muppet", "trumpet", "basket", "casket",
]

TARGET_BYTES = 1024


def random_attr_name(used: set) -> str:
    for _ in range(1000):
        name = f"user.{random.choice(ADJECTIVES)}-{random.choice(NOUNS)}"
        if name not in used:
            return name
    base = f"user.{random.choice(ADJECTIVES)}-{random.choice(NOUNS)}"
    i = 2
    while f"{base}-{i}" in used:
        i += 1
    return f"{base}-{i}"


def random_value(length: int) -> bytes:
    # Pull words from lorem sentences and trim/pad to exact length
    text = ""
    while len(text) < length:
        text += lorem.sentence() + " "
    return text[:length].encode()


def add_xattrs(path: str) -> int:
    """Add xattrs to path until TARGET_BYTES added. Returns bytes added."""
    used_names = set()
    total = 0
    while total < TARGET_BYTES:
        remaining = TARGET_BYTES - total
        if remaining < 40:
            length = remaining
        else:
            length = random.randint(40, min(200, remaining))
        name = random_attr_name(used_names)
        used_names.add(name)
        value = random_value(length)
        os.setxattr(path, name, value)
        total += len(value)
    return total


def main():
    parser = argparse.ArgumentParser(
        description=f"Add random xattrs to files until {TARGET_BYTES} bytes "
                    "of xattr values are added."
    )
    parser.add_argument("files", nargs="+", help="Files to add xattrs to")
    args = parser.parse_args()

    errors = 0
    for path in args.files:
        try:
            added = add_xattrs(path)
            print(f"  {path}  ({added:,} bytes in xattrs)")
        except OSError as e:
            print(f"  {path}  error: {e}", file=sys.stderr)
            errors += 1

    if errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
