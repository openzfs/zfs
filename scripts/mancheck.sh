#!/bin/sh
#
# Permission to use, copy, modify, and/or distribute this software for
# any purpose with or without fee is hereby granted.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
# AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
# OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# shellcheck disable=SC2086,SC2250

trap 'rm -f "$stdout_file" "$stderr_file" "$result_file"' EXIT

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 manpage-directory..."
    exit 1
fi

if ! command -v mandoc > /dev/null; then
    echo "skipping mancheck because mandoc is not installed"
    exit 0
fi

IFS="
"
files="$(find "$@" -type f -name '*[1-9]*')" || exit 1

add_excl="$(awk '
    /^.\\" lint-ok:/ {
        print "-e"
        $1 = "mandoc:"
        $2 = FILENAME ":[[:digit:]]+:[[:digit:]]+:"
        print
    }' $files)"

# Redirect to file instead of 2>&1ing because mandoc flushes inconsistently(?) which tears lines
# https://github.com/openzfs/zfs/pull/12129/checks?check_run_id=2701608671#step:5:3
stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
mandoc -Tlint $files 1>"$stdout_file" 2>"$stderr_file"
result_file="$(mktemp)"
grep -vhE -e 'mandoc: outdated mandoc.db' -e 'STYLE: referenced manual not found' $add_excl "$stdout_file" "$stderr_file" > "$result_file"

if [ -s "$result_file" ]; then
    cat "$result_file"
    exit 1
else
    echo "no errors found"
fi
