#!/usr/bin/env bash
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
# Compare the canonical event schema (events-schema.json) against the
# kernel header (include/sys/zfs_events.h).  Exits 0 when in sync, 1 on
# drift, 2 on usage or extraction errors.
#
# Field names must match exactly on both sides.  Types are compared only
# where the header comment declares one ("uint16:", "uint64:", "string:");
# fields whose header comment carries no type are matched by name alone.

set -euo pipefail

if [[ $# -gt 1 ]]; then
	echo "usage: $0 [repo-root]" >&2
	exit 2
fi

if [[ $# -eq 1 ]]; then
	root="$1"
else
	root="$(cd "$(dirname "$0")/../.." && pwd)"
fi

header="$root/include/sys/zfs_events.h"
schema="$root/contrib/zmetad/events-schema.json"
blob_src="$root/contrib/zmetad/zmetad_schema.c"

[[ -f "$header" ]] || { echo "missing header: $header" >&2; exit 2; }
[[ -f "$schema" ]] || { echo "missing schema: $schema" >&2; exit 2; }
[[ -f "$blob_src" ]] || { echo "missing blob source: $blob_src" >&2; exit 2; }

# "name"  <tab>...  "value"  <tab>/* type: comment */  ->  name
hdr_names="$(sed -n 's/^#define[[:space:]]*ZFS_EV_[A-Z_]*[[:space:]]*"\([^"]*\)".*/\1/p' \
	"$header" | LC_ALL=C sort)"

# Same, but only entries whose comment declares a type:  name|type
hdr_typed="$(sed -n 's/^#define[[:space:]]*ZFS_EV_[A-Z_]*[[:space:]]*"\([^"]*\)".*\*[[:space:]]*\(uint16\|uint64\|string\):.*/\1|\2/p' \
	"$header" | LC_ALL=C sort)"

[[ -n "$hdr_names" ]] || { echo "no ZFS_EV_* fields found in header" >&2; exit 2; }

hdr_version="$(sed -n 's/^#define[[:space:]]*ZFS_EVENTS_VERSION[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
	"$header")"

[[ -n "$hdr_version" ]] || { echo "ZFS_EVENTS_VERSION not found in header" >&2; exit 2; }

json_names="$(python3 -c '
import json, sys
with open(sys.argv[1]) as f:
	doc = json.load(f)
fields = doc["record_format"]["fields"]
for name in sorted(fields):
	print(name)
' "$schema" | LC_ALL=C sort)"

json_typed="$(python3 -c '
import json, sys
with open(sys.argv[1]) as f:
	doc = json.load(f)
fields = doc["record_format"]["fields"]
for name in sorted(fields):
	print(name + "|" + fields[name]["type"])
' "$schema")"

json_version="$(python3 -c '
import json, sys
with open(sys.argv[1]) as f:
	doc = json.load(f)
print(doc["schema_version"])
' "$schema")"

rc=0

if [[ "$json_version" != "$hdr_version" ]]; then
	echo "schema_version mismatch: header=$hdr_version json=$json_version" >&2
	rc=1
fi

if [[ "$hdr_names" != "$json_names" ]]; then
	echo "field name drift between $header and $schema" >&2
	echo "--- header fields ---" >&2
	echo "$hdr_names" >&2
	echo "--- json fields ---" >&2
	echo "$json_names" >&2
	rc=1
fi

# Where the header declares a type, the JSON must agree.
while IFS='|' read -r name typ; do
	[[ -n "$name" ]] || continue
	json_line="$(printf '%s\n' "$json_typed" | grep -Fx "$name|$typ" || true)"
	json_any="$(printf '%s\n' "$json_typed" | grep -F "$name|" || true)"
	if [[ -n "$json_any" && -z "$json_line" ]]; then
		echo "type mismatch for '$name': header=$typ json=$json_any" >&2
		rc=1
	fi
done <<<"$hdr_typed"

# The embedded blob (ZMETAD_EMBEDDED_SCHEMA_JSON in zmetad_schema.c)
# must be byte-identical to events-schema.json: schema_blob.h promises
# this file verifies it.  Extract the C string literal and decode it.
blob_tmp="$(mktemp "${TMPDIR:-/tmp}/schema-blob.XXXXXX.json")"
trap 'rm -f "$blob_tmp"' EXIT

if ! python3 -c '
import re, sys

with open(sys.argv[1]) as f:
	src = f.read()

start = src.find("const char *ZMETAD_EMBEDDED_SCHEMA_JSON")
if start < 0:
	sys.exit("blob definition ZMETAD_EMBEDDED_SCHEMA_JSON not found")
eq = src.find("=", start)
# The literal region ends at the first quote-semicolon pair; inside the
# C literals every quote is escaped (backslash-quote), so "; can only
# occur at the terminator.
end = src.find("\";", eq)
if eq < 0 or end < 0:
	sys.exit("cannot locate bounds of blob literal")
region = src[eq + 1:end + 1]

out = []
# Concatenated adjacent string literals: decode each and join.
for lit in re.findall(r"\"((?:[^\"\\]|\\.)*)\"", region):
	out.append(lit.encode("utf-8").decode("unicode_escape"))
if not out:
	sys.exit("no string literals found in blob region")

with open(sys.argv[2], "wb") as f:
	f.write("".join(out).encode("utf-8"))
' "$blob_src" "$blob_tmp"; then
	echo "cannot extract embedded schema blob from $blob_src" >&2
	exit 2
fi

if ! diff -u "$schema" "$blob_tmp" >&2; then
	echo "embedded schema blob in $blob_src has drifted from $schema:" >&2
	echo "regenerate the blob or revert the JSON change" >&2
	rc=1
fi

if [[ $rc -eq 0 ]]; then
	echo "schema in sync"
fi
exit "$rc"
