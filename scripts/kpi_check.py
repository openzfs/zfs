#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
from pathlib import Path


SYM_RE = re.compile(r'[_][A-Za-z0-9_.$]+')  # good enough for C + mangled C++

def run(cmd: list[str]) -> str:
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        print(p.stdout, end="")
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return p.stdout

def parse_nm_undef(nm_out: str) -> set[str]:
    """
    nm -um output example:
      (undefined) external _IODelay (dynamically looked up)
    We extract the _SYMBOL token(s).
    """
    syms: set[str] = set()
    for line in nm_out.splitlines():
        if "(undefined)" not in line:
            continue
        m = SYM_RE.search(line)
        if m:
            syms.add(m.group(0))   # keep leading underscore to match kmutil output
    return syms

def parse_kmutil_all_symbols(kmutil_out: str) -> set[str]:
    """
    kmutil libraries --all-symbols output lines look like:
      _vnop_copyfile_desc in <path>: com.apple.kpi.bsd (25.1.0)
    We take the first token starting with '_' from each such line.
    """
    syms: set[str] = set()
    for line in kmutil_out.splitlines():
        line = line.strip()
        if not line.startswith("_") and not line.startswith("__Z") and not line.startswith("_Z"):
            # still might start with '_' but keep this fast
            pass
        # Most relevant lines have: "<sym> in <path>: <bundle> (<ver>)"
        if " in " not in line:
            continue
        # symbol is the first whitespace-separated token
        tok = line.split(None, 1)[0]
        if tok.startswith("_") or tok.startswith("__"):
            syms.add(tok)
    return syms

def load_set_file(path: Path) -> set[str]:
    s: set[str] = set()
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        s.add(line)
    return s

def save_set_file(path: Path, syms: set[str]) -> None:
    path.write_text("\n".join(sorted(syms)) + "\n")

def main() -> None:
    ap = argparse.ArgumentParser(
        description="Bruteforce KPI symbol check: undef(kext) minus provided(kmutil libraries)."
    )
    ap.add_argument("-k", "--kext", required=True, help="Path to .kext bundle (directory)")
    ap.add_argument("-b", "--baseline", help="Optional baseline file of allowed-missing symbols")
    ap.add_argument("--write-baseline", action="store_true",
                    help="Write current missing set to --baseline and exit 0")
    ap.add_argument("--arch", default=None,
                    help="Optional arch to pass to nm, e.g. arm64e or x86_64 (uses lipo slices)")
    ap.add_argument("--limit", type=int, default=200, help="Max missing symbols to print")
    args = ap.parse_args()

    kext = Path(args.kext)
    macho = kext / "Contents" / "MacOS" / kext.stem  # e.g. zfs.kext/Contents/MacOS/zfs
    if not macho.exists():
        raise SystemExit(f"Mach-O not found: {macho}")

    nm_cmd = ["nm", "-um"]
    if args.arch:
        nm_cmd += ["-arch", args.arch]
    nm_cmd += [str(macho)]
    nm_out = run(nm_cmd)
    undef = parse_nm_undef(nm_out)

    km_cmd = ["kmutil", "libraries", "--all-symbols", "-p", str(kext)]
    km_out = run(km_cmd)
    provided = parse_kmutil_all_symbols(km_out)

    missing = sorted(undef - provided)

    # optional baseline of "known OK to be unresolved by this heuristic"
    baseline_set: set[str] = set()
    if args.baseline:
        base_path = Path(args.baseline)
        if base_path.exists():
            baseline_set = load_set_file(base_path)
        if args.write_baseline:
            save_set_file(base_path, set(missing))
            print(f"Wrote baseline missing-set to {base_path} ({len(missing)} symbols).")
            return

    filtered = [s for s in missing if s not in baseline_set]

    print(f"Undef symbols in kext (nm):       {len(undef)}")
    print(f"Provided symbols (kmutil dump):  {len(provided)}")
    print(f"Missing (raw diff):             {len(missing)}")
    if args.baseline:
        print(f"Baseline exceptions loaded:      {len(baseline_set)}")
        print(f"Missing (after baseline filter): {len(filtered)}")

    if filtered:
        print("\nFirst missing symbols:")
        for s in filtered[: args.limit]:
            print(f"  {s}")
        if len(filtered) > args.limit:
            print(f"... ({len(filtered) - args.limit} more)")
        sys.exit(2)

if __name__ == "__main__":
    main()
