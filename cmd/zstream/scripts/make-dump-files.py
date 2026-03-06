#!/usr/bin/env python3
"""Run old and new zstream dump -v on stream files, producing abbreviated dump outputs."""
import argparse
import subprocess
import sys
from pathlib import Path

def abbreviate(filename: str) -> str:
    """Split filename at dashes, take the first letter of each segment, lowercased."""
    stem = Path(filename).stem
    # Strip common compression suffixes to get the logical stem
    for ext in (".zfs", ".gz", ".bz2", ".xz", ".zst", ".lz4"):
        if stem.endswith(ext):
            stem = stem[: -len(ext)]
    return "".join(seg[0] for seg in stem.split("-") if seg).lower()

def run_dump(zstream: Path, stream: Path, output: Path) -> bool:
    """Run `zstream dump -v < stream > output`.  Returns True on success."""
    try:
        with open(stream, "rb") as inf, open(output, "w") as outf:
            proc = subprocess.run(
                [str(zstream), "dump", "-v"],
                stdin=inf,
                stdout=outf,
                stderr=outf,
            )
        if proc.returncode != 0:
            print(
                f"  WARNING: {zstream} exited {proc.returncode} for {stream.name}",
                file=sys.stderr,
            )
            if proc.stderr:
                print(f"    stderr: {proc.stderr.decode(errors='replace').rstrip()}",
                      file=sys.stderr)
        return True
    except Exception as e:
        print(f"  ERROR: {e}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Run old and new zstream dump -v on stream files."
    )
    parser.add_argument("old_zstream", type=Path, help="Path to old zstream binary")
    parser.add_argument("new_zstream", type=Path, help="Path to new zstream binary")
    parser.add_argument(
        "streams", nargs="+", type=Path, help="Compressed stream files to process"
    )
    args = parser.parse_args()

    for zs in (args.old_zstream, args.new_zstream):
        if not zs.is_file():
            parser.error(f"zstream binary not found: {zs}")

    for stream in args.streams:
        if not stream.is_file():
            print(f"Skipping missing file: {stream}", file=sys.stderr)
            continue

        abbrev = abbreviate(stream.name)
        out_dir = stream.parent

        old_out = out_dir / f"{abbrev}-old.dump"
        new_out = out_dir / f"{abbrev}-new.dump"

        print(f"{stream.name} -> {abbrev}")

        print(f"  old: {old_out}")
        run_dump(args.old_zstream, stream, old_out)

        print(f"  new: {new_out}")
        run_dump(args.new_zstream, stream, new_out)


if __name__ == "__main__":
    main()
