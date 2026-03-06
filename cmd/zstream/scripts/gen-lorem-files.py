#!/tmp/zstream-venv/bin/python3
"""Generate files with random names filled with lorem ipsum paragraphs."""

import argparse
import random
import sys
from pathlib import Path
from lorem_text import lorem

ADJECTIVES = [
    "boogie", "funky", "wobbly", "snazzy", "jazzy", "groovy", "zippy",
    "bouncy", "fluffy", "crunchy", "sparkly", "fuzzy", "spiffy", "dandy",
    "peppy", "snappy", "sassy", "zesty", "swanky", "nifty", "plucky",
    "quirky", "wacky", "goofy", "dizzy", "breezy", "cheery", "perky",
    "frisky", "chirpy", "feisty", "jolly", "lively", "merry", "spunky",
    "frisky", "zippy", "vivid", "brisk", "sunny", "witty", "kinky",
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

def random_name(used: set) -> str:
    for _ in range(1000):
        name = f"{random.choice(ADJECTIVES)}-{random.choice(NOUNS)}"
        if name not in used:
            return name
    # Fallback: append a number
    base = f"{random.choice(ADJECTIVES)}-{random.choice(NOUNS)}"
    i = 2
    while f"{base}-{i}" in used:
        i += 1
    return f"{base}-{i}"


def fill_file(path: Path, target_size: int, repeat=False) -> None:
    content_parts = []
    total = 0
    para = lorem.paragraph()
    while total < target_size:
        content_parts.append(para)
        total += len(para) + 1  # +1 for newline
        if not repeat:
             para = lorem.paragraph()
    path.write_text("\n\n".join(content_parts) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate files with random names and lorem ipsum content."
    )
    parser.add_argument("count", type=int, help="Number of files to create")
    parser.add_argument("-d", "--directory", default=".", help="Target directory (default: .)")
    parser.add_argument("-r", "--repeat", action="store_true", help="Fill files with reps of a single paragraph")
    parser.add_argument("--min-size", type=int, default=16384, help="Minimum file size in bytes (default: 2048)")
    parser.add_argument("--max-size", type=int, default=128000, help="Maximum file size in bytes (default: 128000)")
    args = parser.parse_args()

    if args.min_size >= args.max_size:
        print(f"error: min-size ({args.min_size}) must be less than max-size ({args.max_size})", file=sys.stderr)
        sys.exit(1)

    directory = Path(args.directory)
    directory.mkdir(parents=True, exist_ok=True)

    used_names = set()
    for i in range(args.count):
        name = random_name(used_names)
        used_names.add(name)
        target_size = random.randint(args.min_size, args.max_size)
        path = directory / name
        fill_file(path, target_size, args.repeat)
        print(f"  {path}  ({path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
