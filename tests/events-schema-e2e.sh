#!/usr/bin/env bash
#
# tests/events-schema-e2e.sh - end-to-end validation of the extended
# metadata event/schema feature chain against a live system.
#
# Chain under test: schema export -> schema check (ok + version-refusing
# bad file) -> wire schema_version presence -> zmetad daemon run ->
# multi-op event capture (CREATE/RENAME/TRUNCATE/SYMLINK) -> SQLite
# assertions -> daemon version refusal on stale meta version.
#
# Usage: bash tests/events-schema-e2e.sh
# Run as root or as a user with passwordless sudo (dataset and file ops
# under /testpool are root-owned).  Exit 0 = whole chain verified;
# exit 1 = first failed assertion printed.  Idempotent: fresh dataset
# per run (e2e-<pid>), and an EXIT trap destroys this run's dataset,
# systemd unit and workdir even on failure.
#
# Environment overrides: ZMETAD, ZFS, ZPOOL.
#
# APPROVED DEVIATIONS from the leaf spec (orchestrator-approved; each
# verified against the live system during cycle-1 reconnaissance):
#
# 1. check-bad: a bare {"schema_version":99} file fails with "schema
#    defines no fields" BEFORE the version check (zmetad_schema.c:606
#    vs :611), so it cannot exercise version refusal.  The bad file is
#    instead built from the EXPORTED schema with schema_version edited
#    to 99; expected rc=1 and a stderr naming versions 99 and 1.
#
# 2b. TRUNCATE records carry object_id + sizes but NO name, so the
#    assertion joins the TRUNCATE row to the RENAME row by object_id
#    rather than by path.  (The kernel fix that makes truncation via
#    setattr emit TRUNCATE at all is in zfs_vnops_os.c zfs_setattr.)
#
# 2. Path assertions use names as actually emitted on the wire: the
#    kernel logs dataset-relative names with NO slash-joined paths
#    (0/19 recon records contained '/'), and zmetad_db.c binds
#    name->path / old_name->old_path directly.  RENAME therefore
#    asserts path='f2', old_path='f1' (NOT 'a/f2'/'a/f1').
#
# 3. The events table has a `size` column (bound from new_size), not
#    `new_size`; the TRUNCATE assertion checks size=0.

set -u

ZFS="${ZFS:-/usr/local/sbin/zfs}"
ZPOOL="${ZPOOL:-/usr/local/sbin/zpool}"
UNIT="zmd-e2e"
POOL="testpool"
BASE="fs1"
BASE_DS="${POOL}/${BASE}"
DS_NAME="e2e-$$"
DS="${BASE_DS}/${DS_NAME}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SCHEMA_FILE="${REPO}/contrib/zmetad/events-schema.json"

CREATED_DS=0
WD=""
WIRE_HAS_VERSION=0
WIRE_VERSION=
ZMETAD="${ZMETAD:-}"
SUDO=()
if [ "$(id -u)" -ne 0 ]; then
	SUDO=(sudo)
fi

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
notice() { printf 'NOTICE: %s\n' "$1"; }

cleanup() {
	status=$?
	"${SUDO[@]}" systemctl stop "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	if [ "$CREATED_DS" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS" >/dev/null 2>&1 || true
	fi
	if [ -n "$WD" ] && [ "${ZMD_E2E_KEEP:-0}" != "1" ]; then
		rm -rf "$WD"
	elif [ -n "$WD" ]; then
		notice "keeping workdir $WD (ZMD_E2E_KEEP=1)"
	fi
	exit "$status"
}
trap cleanup EXIT

# db_has_rename <db> <dataset>: rc 0 when a RENAME row exists.
db_has_rename() {
	python3 - "$1" "$2" <<'PY'
import sqlite3
import sys

try:
    con = sqlite3.connect("file:%s?mode=ro" % sys.argv[1], uri=True,
                          timeout=2)
    n = con.execute(
        "select count(*) from events "
        "where dataset=? and event_type='RENAME'",
        (sys.argv[2],)).fetchone()[0]
except sqlite3.Error:
    sys.exit(1)
sys.exit(0 if n > 0 else 1)
PY
}

step_preflight() {
	"$ZPOOL" list -H -o name "$POOL" >/dev/null 2>&1 ||
		fail "preflight: pool $POOL not imported"

	# Stop/reset any unit left behind by an interrupted earlier run.
	"${SUDO[@]}" systemctl stop "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true

	# Destroy stale e2e-* datasets from interrupted earlier runs.
	while read -r d; do
		case "$d" in
		*/e2e-*) "${SUDO[@]}" "$ZFS" destroy -R "$d" >/dev/null 2>&1 || true ;;
		esac
	done < <("$ZFS" list -r -H -o name "$BASE_DS" 2>/dev/null)

	v="$("$ZFS" get -H -o value events "$BASE_DS" 2>/dev/null)"
	if [ "$v" != "on" ]; then
		"${SUDO[@]}" "$ZFS" set events=on "$BASE_DS" ||
			fail "preflight: cannot set events=on on $BASE_DS"
	fi

	WD="$(mktemp -d /tmp/zmd-e2e.XXXXXX)"

	if [ -n "$ZMETAD" ] && [ -x "$ZMETAD" ]; then
		:
	elif [ -x "${HOME}/git/zfs-metadata/zmetad" ]; then
		ZMETAD="${HOME}/git/zfs-metadata/zmetad"
	elif [ -x /home/caimlas/git/zfs-metadata/zmetad ]; then
		ZMETAD=/home/caimlas/git/zfs-metadata/zmetad
	elif [ -x /usr/local/sbin/zmetad ]; then
		ZMETAD=/usr/local/sbin/zmetad
	else
		fail "preflight: no zmetad binary found (set ZMETAD)"
	fi
	pass preflight
}

step_export() {
	"$ZMETAD" --export-schema "$WD/schema.json" ||
		fail "export: zmetad --export-schema failed"
	cmp -s "$WD/schema.json" "$SCHEMA_FILE" ||
		fail "export: schema differs from $SCHEMA_FILE"
	pass export
}

step_check_ok() {
	"$ZMETAD" --check-schema "$WD/schema.json" >/dev/null 2>&1 ||
		fail "check-ok: --check-schema rejected the exported schema"
	pass check-ok
}

step_check_bad() {
	# Deviation 1: full schema with schema_version edited to 99.
	python3 - "$WD/schema.json" "$WD/bad.json" <<'PY'
import json
import sys

with open(sys.argv[1]) as f:
    s = json.load(f)
s["schema_version"] = 99
with open(sys.argv[2], "w") as f:
    json.dump(s, f)
PY
	err="$("$ZMETAD" --check-schema "$WD/bad.json" 2>&1 1>/dev/null)"
	rc=$?
	[ "$rc" -eq 1 ] || fail "check-bad: expected rc=1, got rc=$rc"
	case "$err" in
	*99*) ;;
	*) fail "check-bad: stderr does not name version 99: $err" ;;
	esac
	case "$err" in
	*version*) ;;
	*) fail "check-bad: stderr does not name a version mismatch: $err" ;;
	esac
	pass check-bad
}

step_module_version() {
	# Advisory only: on a pre-swap module schema_version is absent from
	# the wire; skip with a notice rather than fail.
	# NOTE: schema_version is a top-level key in the GET_EVENTS ioctl
	# reply, which `zfs events -j` does NOT print (it emits only the
	# record array). Probe the raw ioctl with a tiny lzc_get_events
	# program instead; fall back to the CLI grep if the compile fails.
	probe="/tmp/e2e-wireprobe.$$.c"
	bin="/tmp/e2e-wireprobe.$$"
	cat > "$probe" <<'EOF'
#include <stdio.h>
#include <libzfs/sys/nvpair.h>
#include <libzfs/libzfs_core.h>
int main(int argc, char **argv) {
	nvlist_t *page = NULL;
	uint64_t v = 0;
	libzfs_core_init();
	if (lzc_get_events(argv[1], 0, 0, &page) != 0)
		return 2;
	if (nvlist_lookup_uint64(page, "schema_version", &v) == 0) {
		printf("%llu\n", (unsigned long long)v);
		return 0;
	}
	return 3;
}
EOF
	wire=""
	if gcc -I "$REPO/include" -I "$REPO/lib/libspl/include" \
	    "$probe" -o "$bin" \
	    -L "$REPO/lib/libzfs_core/.libs" -lzfs_core -lnvpair \
	    >/dev/null 2>&1; then
		wire="$(sudo env \
		    LD_LIBRARY_PATH="$REPO/lib/libzfs_core/.libs:$REPO/lib/libnvpair/.libs" \
		    "$bin" "$BASE_DS" 2>/dev/null || true)"
		case "$wire" in
		[0-9]*)
			WIRE_HAS_VERSION=1
			WIRE_VERSION="$wire"
			pass module-version
			rm -f "$probe" "$bin"
			return
			;;
		esac
	fi
	rm -f "$probe" "$bin"
	wire="$("$ZFS" events -j "$BASE_DS" 2>/dev/null | head -c 2000 || true)"
	case "$wire" in
	*schema_version*)
		WIRE_HAS_VERSION=1
		pass module-version
		;;
	*)
		notice "module-version: schema_version absent from wire events; wire-version assertion skipped"
		;;
	esac
}

step_daemon_run() {
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		--description="zmetad e2e validation" \
		"$ZMETAD" -f -i 3 -d "$WD/zmd.db" ||
		fail "daemon-run: systemd-run failed"
	sleep 4
	"${SUDO[@]}" systemctl is-active --quiet "$UNIT" ||
		fail "daemon-run: unit $UNIT not active after start"
	pass daemon-run
}

step_ops() {
	"${SUDO[@]}" "$ZFS" create "$DS" || fail "ops: zfs create $DS failed"
	CREATED_DS=1
	"${SUDO[@]}" "$ZFS" set events=on "$DS" ||
		fail "ops: zfs set events=on $DS failed"
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"
	case "$mnt" in
	/*) ;;
	*) fail "ops: unexpected mountpoint '$mnt'" ;;
	esac
	"${SUDO[@]}" mkdir "$mnt/a" || fail "ops: mkdir a"
	"${SUDO[@]}" dd if=/dev/zero of="$mnt/a/f1" bs=4096 count=1 \
		status=none || fail "ops: write a/f1"
	"${SUDO[@]}" mv "$mnt/a/f1" "$mnt/a/f2" || fail "ops: rename a/f1 a/f2"
	"${SUDO[@]}" sh -c ": > '$mnt/a/f2'" || fail "ops: truncate a/f2"
	"${SUDO[@]}" ln -s tgt "$mnt/a/l1" || fail "ops: symlink a/l1"

	# Poll interval is 3s; allow up to 30s for the RENAME row to land.
	found=0
	i=0
	while [ "$i" -lt 15 ]; do
		if db_has_rename "$WD/zmd.db" "$DS"; then
			found=1
			break
		fi
		sleep 2
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] || fail "ops: RENAME row never appeared within 30s"
	pass ops
}

step_assert() {
	# Deviations 2 and 3 applied below: dataset-relative wire names,
	# `size` column for TRUNCATE.
	python3 - "$WD/zmd.db" "$DS" "$WD/schema.json" <<'PY'
import json
import sqlite3
import sys

db, ds, schema_path = sys.argv[1], sys.argv[2], sys.argv[3]
with open(schema_path) as f:
    file_version = str(json.load(f)["schema_version"])
con = sqlite3.connect("file:%s?mode=ro" % db, uri=True, timeout=5)
errors = []

ev = con.execute(
    "select event_type, path, old_path, txg, object_id, size "
    "from events where dataset=?", (ds,)).fetchall()

def has(t, p, old=None):
    for r in ev:
        if r[0] == t and r[1] == p and (old is None or r[2] == old):
            return r
    return None

if has("CREATE", "a") is None:
    errors.append("no CREATE row for path 'a'")
if has("CREATE", "f1") is None:
    errors.append("no CREATE row for path 'f1'")
if has("RENAME", "f2", "f1") is None:
    errors.append("no RENAME row with path='f2' old_path='f1'")
# TRUNCATE records carry object_id + sizes but no name (kernel logs dataset-
# relative names only on create/rename/etc); the file is identified by
# joining on the RENAME row's object_id.
ren = has("RENAME", "f2", "f1")
tr = None
for row in ev:
    if row[0] == "TRUNCATE" and ren is not None and row[4] == ren[4]:
        tr = row
        break
if tr is None:
    errors.append("no TRUNCATE row for object_id of 'f2'")
elif tr[5] != 0:
    errors.append("TRUNCATE row size=%r, expected 0" % (tr[5],))
if has("SYMLINK", "l1") is None:
    errors.append("no SYMLINK row for path 'l1'")
for r in ev:
    if r[3] is None or r[3] <= 0:
        errors.append("row %s/%s has txg=%r" % (r[0], r[1], r[3]))
    if r[4] is None or r[4] <= 0:
        errors.append("row %s/%s has object_id=%r" % (r[0], r[1], r[4]))

meta = con.execute(
    "select value from meta where key='events_schema_version'").fetchall()
if not meta:
    print("NOTICE: meta events_schema_version absent (wire version "
          "0/absent); not fatal")
elif meta[0][0] != file_version:
    errors.append("meta events_schema_version=%s, expected %s"
                  % (meta[0][0], file_version))

if errors:
    for e in errors:
        print("ASSERT FAIL: %s" % e)
    sys.exit(1)
PY
	[ $? -eq 0 ] || fail "assert: see ASSERT FAIL lines above"
	pass assert
}

step_version_refusal() {
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "version-refusal: failed to stop daemon"
	python3 - "$WD/zmd.db" <<'PY'
import sqlite3
import sys

con = sqlite3.connect(sys.argv[1])
con.execute(
    "insert or replace into meta values "
    "('events_schema_version', '99')")
con.commit()
PY
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		"$ZMETAD" -f -i 3 -d "$WD/zmd.db" ||
		fail "version-refusal: restart failed"
	died=0
	i=0
	while [ "$i" -lt 10 ]; do
		if ! "${SUDO[@]}" systemctl is-active --quiet "$UNIT"; then
			died=1
			break
		fi
		sleep 1
		i=$((i + 1))
	done
	[ "$died" -eq 1 ] ||
		fail "version-refusal: daemon still running after 10s with meta version 99"
	log="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager -n 100 2>/dev/null || true)"
	case "$log" in
	*99*) ;;
	*) fail "version-refusal: refusal output does not name 99" ;;
	esac
	case "$log" in
	*version*) ;;
	*) fail "version-refusal: refusal output does not mention version" ;;
	esac
	pass version-refusal
}

step_preflight
step_export
step_check_ok
step_check_bad
step_module_version
step_daemon_run
step_ops
step_assert
step_version_refusal

printf 'E2E: ALL PASS\n'
exit 0
