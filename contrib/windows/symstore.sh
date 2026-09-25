#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

ARCH="${1:-x64}"
CONFIG="${2:-Debug}"
VERSION="${3:-$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo unknown)}"

BUILD_DIR="$ROOT/out/build/${ARCH}-${CONFIG}"
ARCHIVE_ROOT="$ROOT/symbols"
SYMSTORE_ROOT="$ROOT/symstore"
ARCHIVE_DIR="$ARCHIVE_ROOT/${VERSION}/${ARCH}-${CONFIG}"

# Adjust if your symstore.exe lives elsewhere
SYMSTORE_EXE="/c/Program Files (x86)/Windows Kits/10/Debuggers/x64/symstore.exe"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Build dir not found: $BUILD_DIR"
    exit 1
fi

if [[ ! -x "$SYMSTORE_EXE" ]]; then
    echo "symstore.exe not found:"
    echo "  $SYMSTORE_EXE"
    exit 1
fi

mkdir -p "$ARCHIVE_DIR"
mkdir -p "$SYMSTORE_ROOT"

TMP_LIST="$(mktemp)"
trap 'rm -f "$TMP_LIST"' EXIT

echo "Scanning build output under:"
echo "  $BUILD_DIR"
echo

mapfile -t FILES < <(
    find "$BUILD_DIR" \
        \( -name '*.pdb' -o -name '*.sys' -o -name '*.exe' \) \
        ! -path '*/CMakeFiles/*' \
        ! -path '*/Testing/*' \
        -type f \
        | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No .pdb/.sys/.exe files found."
    exit 1
fi

echo "Files to archive/store:"
printf '  %s\n' "${FILES[@]}"
echo

for f in "${FILES[@]}"; do
    base="$(basename "$f")"
    cp -f "$f" "$ARCHIVE_DIR/$base"
    printf '%s\n' "$(cygpath -w "$f")" >> "$TMP_LIST"
done

echo "Archived files to:"
echo "  $ARCHIVE_DIR"
echo

echo "Adding files to symbol store:"
echo "  $SYMSTORE_ROOT"
echo

"$SYMSTORE_EXE" add \
    -f @"$(cygpath -w "$TMP_LIST")" \
    -s "$(cygpath -w "$SYMSTORE_ROOT")" \
    -t OpenZFS \
    -v "$VERSION-${ARCH}-${CONFIG}"

echo
echo "Done."
echo
echo "Use in WinDbg with:"
echo "  .sympath $(cygpath -w "$SYMSTORE_ROOT");https://msdl.microsoft.com/download/symbols"
echo "  .reload /f"
