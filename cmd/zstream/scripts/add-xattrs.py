#!/tmp/zstream-venv/bin/python3
"""Add random extended attributes to files until 600 bytes of xattrs are added."""

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
    """Add xattrs to path until TARGET_BYTES total value bytes added. Returns bytes added."""
    used_names = set()
    total = 0
    while total < TARGET_BYTES:
        remaining = TARGET_BYTES - total
        length = min(random.randint(40, 200), remaining) if remaining < 40 else random.randint(40, min(200, remaining))
        # If remaining < 40, just do one final attr to hit the target
        if remaining < 40:
            length = remaining
        name = random_attr_name(used_names)
        used_names.add(name)
        value = random_value(length)
        os.setxattr(path, name, value)
        total += len(value)
    return total


def main():
    parser = argparse.ArgumentParser(
        description=f"Add random xattrs to files until {TARGET_BYTES} bytes of xattr values are added."
    )
    parser.add_argument("files", nargs="+", help="Files to annotate with xattrs")
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
