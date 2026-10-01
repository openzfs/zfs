#!/usr/bin/env bash
#
# tests/events-schema-e2e.sh - end-to-end validation of the extended
# metadata event/schema feature chain against a live system.
#
# Chain under test: schema export -> schema check (ok + version-refusing
# bad file) -> wire schema_version presence -> zmetad daemon run ->
# multi-op event capture (CREATE/RENAME/TRUNCATE/SYMLINK) -> SQLite
# assertions -> db_schema_version meta key -> gap-row detection on
# watermark regression -> daemon version refusal on stale meta version.
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
#
# 4. The watermark-regression gap test uses a real ring CLEAR
#    (lzc_clear_events) instead of doctoring sync_state: verified
#    against the kernel, an out-of-range stored watermark is clamped
#    by zfs_events_get() and reports next_offset=0 (the "log
#    exhausted" sentinel), which the daemon must NOT treat as a
#    regression.  The clear resets the ring to offset 0, so the
#    daemon's next poll genuinely regresses below its in-memory
#    high-water mark with lost=0.

set -u

ZFS="${ZFS:-/usr/local/sbin/zfs}"
ZPOOL="${ZPOOL:-/usr/local/sbin/zpool}"
UNIT="zmd-e2e"
POOL="testpool"
BASE="fs1"
BASE_DS="${POOL}/${BASE}"
DS_NAME="e2e-$$"
DS="${BASE_DS}/${DS_NAME}"
SWAP_DS="${BASE_DS}/e2e-swp-$$"
RET_DS="e2e-ret-$$"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SCHEMA_FILE="${REPO}/contrib/zmetad/events-schema.json"

CREATED_DS=0
CREATED_SWAP_DS=0
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
	if [ "$CREATED_SWAP_DS" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$SWAP_DS" >/dev/null 2>&1 || true
	fi
	if [ -n "$WD" ] && [ "${ZMD_E2E_KEEP:-0}" != "1" ]; then
		rm -rf "$WD"
	elif [ -n "$WD" ]; then
		notice "keeping workdir $WD (ZMD_E2E_KEEP=1)"
	fi
	exit "$status"
}
trap cleanup EXIT

# db_query <db> <python-expression using con> : print the value.
db_query() {
	python3 - "$1" "$2" <<'PY'
import sqlite3
import sys

try:
    con = sqlite3.connect("file:%s?mode=ro" % sys.argv[1], uri=True,
                          timeout=2)
    print(eval(sys.argv[2], {"con": con}))
except sqlite3.Error:
    print("ERR")
PY
}

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

	# Normalize the base dataset's kernel ring: a ring grown by
	# earlier runs makes every daemon collect pass take minutes,
	# which starves later steps of polls. Clearing preserves ring
	# identity, so this is invisible to the identity tests below;
	# the daemon clamps its watermark to the new (empty) tail on
	# its next poll.
	"${SUDO[@]}" "$ZFS" events -c "$BASE_DS" >/dev/null 2>&1 || true

	WD="$(mktemp -d /var/tmp/zmd-e2e.XXXXXX)"

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
    "select event_type, path, old_path, txg, object_id, size, "
    "parent, old_parent from events where dataset=?", (ds,)).fetchall()

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

try:
    cols = [r[1] for r in con.execute(
        "pragma table_info(events)").fetchall()]
except sqlite3.Error:
    cols = []
for col in ("parent", "old_parent", "target", "old_size", "attrs"):
    if col not in cols:
        errors.append("events table missing column %s" % col)

try:
    gaps_cols = sorted(r[1] for r in con.execute(
        "pragma table_info(gaps)").fetchall())
except sqlite3.Error:
    gaps_cols = []
if gaps_cols != ["dataset", "detected", "from_offset", "id",
                 "lost", "to_offset"]:
    errors.append("gaps table missing or wrong columns: %r" % (gaps_cols,))

dbver = con.execute(
    "select value from meta where key='db_schema_version'").fetchall()
if not dbver:
    errors.append("meta db_schema_version key absent")
elif dbver[0][0] != "6":
    errors.append("meta db_schema_version=%r, expected '6'"
                  % (dbver[0][0],))
sync_cols = [r[1] for r in con.execute(
    "pragma table_info(sync_state)").fetchall()]
if "ring_guid" not in sync_cols:
    errors.append("sync_state missing ring_guid column: %r" % (sync_cols,))
if "last_lost" not in sync_cols:
    errors.append("sync_state missing last_lost column: %r" % (sync_cols,))
ev_cols = [r[1] for r in con.execute(
    "pragma table_info(events)").fetchall()]
if "captured_at" not in ev_cols:
    errors.append("events missing captured_at column: %r" % (ev_cols,))
if "full_path" not in ev_cols or "old_full_path" not in ev_cols:
    errors.append("events missing full_path columns: %r" % (ev_cols,))

# gh #11: name-bearing CREATE rows resolve to dataset-relative full
# paths at insert time (objmap graph). The ops step creates a/b/c
# under the dataset; every CREATE row's full_path must be non-NULL
# and end with the bare name.
for row in con.execute(
        "select path, full_path from events "
        "where dataset=? and event_type='CREATE'", (ds,)).fetchall():
    _p, _fp = row
    if _fp is None:
        errors.append("CREATE %r has NULL full_path" % (_p,))
    elif not _fp.endswith("/" + _p) and _fp != _p:
        errors.append("CREATE %r full_path=%r does not end in name"
                      % (_p, _fp))
_ren = con.execute(
    "select full_path, old_full_path from events "
    "where dataset=? and event_type='RENAME' limit 1",
    (ds,)).fetchone()
if _ren is not None and (_ren[0] is None or _ren[1] is None):
    errors.append("RENAME full_path/old_full_path NULL: %r" % (_ren,))

# parent is decoded on every name-bearing op; the CREATE row for 'a'
# must carry a non-NULL parent object id, and the RENAME row must
# carry both parent and old_parent.
for label, row in (("CREATE 'a'", has("CREATE", "a")),
                   ("RENAME", ren)):
    if row is None:
        continue
    if len(row) < 7 or row[6] is None:
        errors.append("%s row has parent=%r, expected non-NULL"
                      % (label, row[6] if len(row) > 6 else None))
if ren is not None and (len(ren) < 8 or ren[7] is None):
    errors.append("RENAME row has old_parent=%r, expected non-NULL"
                  % (ren[7] if len(ren) > 7 else None))

if errors:
    for e in errors:
        print("ASSERT FAIL: %s" % e)
    sys.exit(1)
PY
	[ $? -eq 0 ] || fail "assert: see ASSERT FAIL lines above"
	pass assert
}

step_gap() {
	# GH #4: collector-lag loss detection.
	#
	# The kernel-drivable loss signal is the records_lost DELTA
	# between polls (ring wrap: bof advances past the watermark,
	# or drain-queue overflow). A watermark regression
	# (next_offset below the daemon's high-water) is NOT
	# reachable in a cheap test: zfs_events_get() clamps an
	# out-of-range read offset into the current ring window, so
	# after a clear or recreate the daemon's next poll simply
	# resumes at the new tail (empty ring -> next_offset=0, the
	# "log exhausted" sentinel, correctly excluded). The
	# regression branch in zmetad stays as defense-in-depth for
	# ring replacement while the clamp cannot produce it.
	#
	# Drive a REAL wrap. IO auditing must be on: writes with
	# events_io=off emit nothing and records_lost does not move.
	# The log object already exists at its create-time size
	# (events_size does not resize it); 64MiB of 4K writes at
	# window=0 is enough to wrap a 1MiB ring between 3s polls.
	"${SUDO[@]}" "$ZFS" set events_io=on events_io_window=0 "$DS" ||
		fail "gap: set events_io=on events_io_window=0"
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"
	"${SUDO[@]}" dd if=/dev/zero of="$mnt/gapwrap" bs=4096 \
	    count=16384 conv=notrunc status=none ||
		fail "gap: bulk write failed"
	"${SUDO[@]}" rm -f "$mnt/gapwrap"

	found=0
	i=0
	while [ "$i" -lt 15 ]; do
		n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from gaps where ' \
		    'dataset=?', ('$DS',)).fetchone()[0]" 2>/dev/null)"
		case "$n" in
		''|ERR|null|0) ;;
		*) found=1; break ;;
		esac
		sleep 2
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] ||
		fail "gap: no gaps row for $DS within 30s of ring wrap"

	# The gap row must report a positive lost count (a wrap
	# always increments records_lost via advance_bof).
	lost_n="$(db_query "$WD/zmd.db" \
	    "con.execute('select lost from gaps where dataset=? and ' \
	    'lost > 0 limit 1', ('$DS',)).fetchone()" 2>/dev/null)"
	case "$lost_n" in
	''|ERR|null|None) fail "gap: gaps row has no positive lost count" ;;
	esac

	# Restore the fence window for any later steps.
	"${SUDO[@]}" "$ZFS" set events_io_window=1000 "$DS" ||
		fail "gap: restore events_io_window"

	# The daemon must have warned on stderr (journal).
	log="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager -n 200 \
	    2>/dev/null || true)"
	case "$log" in
	*"records lost since last poll"*) ;;
	*) fail "gap: daemon stderr lacks loss warning" ;;
	esac

	pass gap
}

step_restart_loss() {
	# Wrap while the daemon is down. last_lost must already be in
	# sync_state from step_gap; a process-memory baseline re-arms
	# on restart and this step records no new gap.
	local before found i n
	before="$(db_query "$WD/zmd.db" \
	    "con.execute('select coalesce(max(id), 0) from gaps where ' \
	    'dataset=?', ('$DS',)).fetchone()[0]" 2>/dev/null)"
	case "$before" in
	''|ERR|null|None)
		fail "restart-loss: cannot read gaps baseline"
		return
		;;
	esac
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "restart-loss: failed to stop daemon"
	"${SUDO[@]}" "$ZFS" set events_io=on events_io_window=0 "$DS" ||
		fail "restart-loss: set events_io=on"
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"
	"${SUDO[@]}" dd if=/dev/zero of="$mnt/gapdown" bs=4096 \
	    count=16384 conv=notrunc status=none ||
		fail "restart-loss: bulk write failed"
	"${SUDO[@]}" rm -f "$mnt/gapdown"
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		--description="zmetad e2e validation" \
		"$ZMETAD" -f -i 3 -d "$WD/zmd.db" ||
		fail "restart-loss: daemon restart failed"
	found=0
	i=0
	while [ "$i" -lt 15 ]; do
		n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from gaps where ' \
		    'dataset=? and id > ? and lost > 0', ('$DS', $before)).fetchone()[0]" \
		    2>/dev/null)"
		case "$n" in
		''|ERR|null|0|None) ;;
		*) found=1; break ;;
		esac
		sleep 2
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] ||
		fail "restart-loss: no new positive gaps row after restart (before id $before)"
	"${SUDO[@]}" "$ZFS" set events_io_window=1000 "$DS" ||
		fail "restart-loss: restore events_io_window"
	pass restart-loss
}

# step_guid_swap <db> <dataset>: destroy + recreate an events=on child
# between daemon polls and wait (up to 30s, like step_gap) for the
# ring-replace evidence: a gaps row with the lost=-1 sentinel, the
# journal "ring replaced" warning, and a changed sync_state.ring_guid.
# On success echoes the NEW guid so a later step can assert against it.
step_guid_swap() {
	_db="$1"
	_ds="$2"
	_mnt="$("$ZFS" get -H -o value mountpoint "$_ds")"
	case "$_mnt" in
	/*) ;;
	*) fail "guid-swap: unexpected mountpoint '$_mnt'" ;;
	esac

	# One write so the daemon has stamped sync_state (last_offset,
	# ring_guid) for this dataset before the swap.
	"${SUDO[@]}" touch "$_mnt/swp1" ||
		fail "guid-swap: touch swp1 failed"
	_old_guid=""
	_i=0
	while [ "$_i" -lt 15 ]; do
		_old_guid="$(db_query "$_db" \
		    "con.execute('select ring_guid from sync_state ' \\
		    'where dataset=?', ('$_ds',)).fetchone()" 2>/dev/null)"
		case "$_old_guid" in
		''|ERR|null|None|0) ;;
		*) break ;;
		esac
		sleep 2
		_i=$((_i + 1))
	done
	case "$_old_guid" in
	''|ERR|null|None|0)
		fail "guid-swap: no nonzero ring_guid stamped for $_ds within 30s"
		;;
	esac

	# Destroy + recreate between polls. The daemon tolerates a
	# missing dataset (lzc_get_events ENOENT -> silent skip).
	"${SUDO[@]}" "$ZFS" destroy -R "$_ds" ||
		fail "guid-swap: destroy failed"
	"${SUDO[@]}" "$ZFS" create "$_ds" ||
		fail "guid-swap: recreate failed"
	"${SUDO[@]}" "$ZFS" set events=on "$_ds" ||
		fail "guid-swap: events=on after recreate failed"
	_mnt="$("$ZFS" get -H -o value mountpoint "$_ds")"
	"${SUDO[@]}" touch "$_mnt/swp2" ||
		fail "guid-swap: touch swp2 failed"

	# Poll up to 30s for: the -1 gaps row, the journal warning, and
	# a changed guid in sync_state.
	_gap=""
	_i=0
	while [ "$_i" -lt 15 ]; do
		_gap="$(db_query "$_db" \
		    "con.execute('select count(*) from gaps where ' \\
		    'dataset=? and lost=-1', ('$_ds',)).fetchone()[0]" \
		    2>/dev/null)"
		_new_guid="$(db_query "$_db" \
		    "con.execute('select ring_guid from sync_state ' \\
		    'where dataset=?', ('$_ds',)).fetchone()" 2>/dev/null)"
		case "$_gap" in
		''|ERR|null|0) ;;
		*)
			case "$_new_guid" in
			''|ERR|null|None|"$_old_guid") ;;
			*) break ;;
			esac
			;;
		esac
		sleep 2
		_i=$((_i + 1))
	done
	case "$_gap" in
	''|ERR|null|0)
		fail "guid-swap: no lost=-1 gaps row for $_ds within 30s"
		;;
	esac
	case "$_new_guid" in
	''|ERR|null|None|"$_old_guid")
		fail "guid-swap: ring_guid did not change (old=$_old_guid new=$_new_guid)"
		;;
	esac

	_log="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager -n 200 \
	    2>/dev/null || true)"
	case "$_log" in
	*"ring replaced on $_ds"*) ;;
	*) fail "guid-swap: journal lacks 'ring replaced on $_ds' warning" ;;
	esac

	printf '%s' "$_new_guid"
}

step_guid_purge() {
	# Shared state for the swap and purge proofs: a dedicated child
	# dataset so neither pollutes the suite dataset's history.
	"${SUDO[@]}" "$ZFS" create "$SWAP_DS" ||
		fail "guid-purge: create $SWAP_DS failed"
	CREATED_SWAP_DS=1
	"${SUDO[@]}" "$ZFS" set events=on "$SWAP_DS" ||
		fail "guid-purge: events=on on $SWAP_DS failed"

	# --- ring swap: destroy/recreate between daemon polls -> the
	# daemon writes the lost=-1 sentinel, warns on the journal, and
	# re-stamps sync_state.ring_guid. Captured guid flows into the
	# purge assertion below.
	_new_guid="$(step_guid_swap "$WD/zmd.db" "$SWAP_DS")"
	[ "$_new_guid" != "" ] || fail "guid-purge: swap helper returned no guid"

	# --- purge: while the ring still has events, --purge must wipe
	# DB rows (events/gaps/sync_state), clear the kernel ring, exit 0.
	# The live daemon may hold the SQLite write lock at the moment
	# purge runs (database is locked -> rc 1); retry briefly.
	_purge_rc=1
	for _i in 1 2 3 4 5; do
		"${SUDO[@]}" "$ZMETAD" --purge "$SWAP_DS" -d "$WD/zmd.db" &&
			{ _purge_rc=0; break; }
		_purge_rc=$?
		[ "$_purge_rc" = "2" ] && break
		sleep 2
	done
	[ "$_purge_rc" = "0" ] ||
		fail "guid-purge: --purge $SWAP_DS exited nonzero (rc=$_purge_rc)"
	for _t in events gaps sync_state; do
		_n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from $_t where ' \\
		    'dataset=?', ('$SWAP_DS',)).fetchone()[0]" 2>/dev/null)"
		case "$_n" in
		0) ;;
		''|ERR|null)
			fail "guid-purge: cannot count $_t rows for $SWAP_DS"
			;;
		*)
			fail "guid-purge: $_t still has $_n rows for $SWAP_DS after purge"
			;;
		esac
	done
	_ring="$("$ZFS" events -j "$SWAP_DS" 2>/dev/null || true)"
	case "$_ring" in
	""|"[]") ;;
	*) fail "guid-purge: kernel ring not empty after purge: $_ring" ;;
	esac

	# Unknown dataset: exit 2 (run_purge: "dataset not found").
	"${SUDO[@]}" "$ZMETAD" --purge "$BASE_DS/no-such-e2e-$$" \
	    -d "$WD/zmd.db" >/dev/null 2>&1 && \
	    fail "guid-purge: purge of nonexistent dataset exited 0"
	[ $? -eq 2 ] ||
		fail "guid-purge: purge of nonexistent dataset did not exit 2"

	# The swap's -1 row is gone now (purge deleted it) -- and the
	# new ring's guid must still match what sync_state carried at
	# swap time, because purge also cleared sync_state: a fresh
	# poll re-stamps from the SAME kernel ring instance.  The
	# re-stamp needs a poll that reaches the identity write: an
	# empty (just-cleared) ring returns no records, so generate
	# one event to wake the collect path.
	local _mnt
	_mnt="$("$ZFS" get -H -o value mountpoint "$SWAP_DS")"
	"${SUDO[@]}" touch "$_mnt/post-purge-$$"
	_i=0
	while [ "$_i" -lt 60 ]; do
		_after="$(db_query "$WD/zmd.db" \
		    "con.execute('select ring_guid from sync_state ' \
		    'where dataset=?', ('$SWAP_DS',)).fetchone()" 2>/dev/null)"
		case "$_after" in
		"$_new_guid") _i=99; break ;;
		esac
		sleep 2
		_i=$((_i + 1))
	done
	[ "$_i" -eq 99 ] ||
		fail "guid-purge: sync_state.ring_guid not re-stamped to $_new_guid within 120s"

	"${SUDO[@]}" "$ZFS" destroy -R "$SWAP_DS" || true
	CREATED_SWAP_DS=0

	pass guid-purge
}

step_migration() {
	# Backdate to 3 so the v<4, v<5 and v<6 ALTER chain all run on
	# restart (every ALTER is duplicate-column-tolerant).  The version
	# key is stamped only after every column for the build exists.
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "migration: failed to stop daemon"
	"${SUDO[@]}" python3 - "$WD/zmd.db" <<'PY'
import sqlite3
import sys

con = sqlite3.connect(sys.argv[1])
con.execute(
    "insert or replace into meta values "
    "('db_schema_version', '3')")
con.commit()
PY
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		--description="zmetad e2e validation" \
		"$ZMETAD" -f -i 3 -d "$WD/zmd.db" ||
		fail "migration: daemon restart failed"
	sleep 4
	"${SUDO[@]}" systemctl is-active --quiet "$UNIT" ||
		fail "migration: unit $UNIT not active after restart"
	_log="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager --since="-30s" \
	    2>/dev/null || true)"
	case "$_log" in
	*"upgraded database to layout version 6"*) ;;
	*) fail "migration: journal lacks 'upgraded database to layout version 6'" ;;
	esac
	_v="$(db_query "$WD/zmd.db" \
	    "con.execute(\"select value from meta where key='db_schema_version'\").fetchone()[0]" \
	    2>/dev/null)"
	[ "$_v" = "6" ] ||
		fail "migration: meta db_schema_version=$_v, expected '6'"
	pass migration
}

step_retention() {
	# Gaps-retention guarantee: events captured_at older than the
	# retention window are deleted; gaps rows are NEVER touched by
	# cleanup (only --purge removes them, per SCHEMA.md). The
	# daemon runs its cleanup pass on the first loop tick, so a
	# restart with -r 1 exercises it without waiting a day.
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "retention: failed to stop daemon"

	# Synthetic history, all scoped to a dataset name that never
	# exists in the kernel (written straight into the suite DB):
	# an old events row (captured_at = now-7d) that the 1-day
	# window must delete, plus old/recent/-1 gaps rows that must
	# all survive.
	"${SUDO[@]}" python3 - "$WD/zmd.db" "$RET_DS" <<'PY'
import sqlite3
import sys
import time

db, ds = sys.argv[1], sys.argv[2]
now = int(time.time())
con = sqlite3.connect(db)
con.execute(
    "insert into events (dataset, txg, timestamp, object_id, "
    "event_type, captured_at) values (?,?,?,?,?,?)",
    (ds, 1, now - 7 * 86400, 1, "WRITE", now - 7 * 86400))
con.execute(
    "insert into gaps (dataset, detected, from_offset, "
    "to_offset, lost) values (?,?,?,?,?)",
    (ds, now - 7 * 86400, 0, 100, 7))
con.execute(
    "insert into gaps (dataset, detected, from_offset, "
    "to_offset, lost) values (?,?,?,?,?)",
    (ds, now - 30 * 86400, 0, 100, 0))
con.execute(
    "insert into gaps (dataset, detected, from_offset, "
    "to_offset, lost) values (?,?,?,?,?)",
    (ds, now, 0, 100, -1))
con.commit()
PY

	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		--description="zmetad e2e validation" \
		"$ZMETAD" -f -i 3 -r 1 -d "$WD/zmd.db" ||
		fail "retention: daemon restart failed"
	sleep 6
	"${SUDO[@]}" systemctl is-active --quiet "$UNIT" ||
		fail "retention: unit $UNIT not active after restart"

	# The cleanup pass runs on the daemon's first loop tick, AFTER
	# the first collect finishes - and a collect can take tens of
	# seconds on a dataset with a large kernel ring. Poll for the
	# deletion instead of sleeping a fixed interval.
	_ev=""
	for _i in $(seq 1 30); do
		_ev="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from events where ' \\
		    'dataset=?', ('$RET_DS',)).fetchone()[0]" 2>/dev/null)"
		[ "$_ev" = "0" ] && break
		sleep 2
	done
	[ "$_ev" = "0" ] ||
		fail "retention: old events row survived 1d retention (count=$_ev)"
	_gap_lost="$(db_query "$WD/zmd.db" \
	    "sorted(r[0] for r in con.execute('select lost from gaps ' \\
	    'where dataset=?', ('$RET_DS',)).fetchall())" 2>/dev/null)"
	[ "$_gap_lost" = "[-1, 0, 7]" ] ||
		fail "retention: gaps rows not intact, lost values=$_gap_lost (expected [-1, 0, 7])"

	pass retention
}

step_purge() {
	# One-shot purge of the suite dataset (daemon still running and
	# holding history for it from step_ops/step_gap): rc 0, all
	# three tables empty for $DS, kernel ring cleared.  Retry on a
	# locked database (live daemon write contention), like the
	# guid-purge step.
	_purge_rc=1
	for _i in 1 2 3 4 5; do
		"${SUDO[@]}" "$ZMETAD" --purge "$DS" -d "$WD/zmd.db" &&
			{ _purge_rc=0; break; }
		_purge_rc=$?
		[ "$_purge_rc" = "2" ] && break
		sleep 2
	done
	[ "$_purge_rc" = "0" ] ||
		fail "purge: --purge $DS exited nonzero (rc=$_purge_rc)"
	for _t in events gaps sync_state; do
		_n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from $_t where ' \\
		    'dataset=?', ('$DS',)).fetchone()[0]" 2>/dev/null)"
		case "$_n" in
		0) ;;
		# A poll landing inside the purge->count window re-writes
		# the live dataset's sync_state watermark; events/gaps must
		# still be strictly 0.
		1) [ "$_t" = "sync_state" ] ||
			fail "purge: $_t has 1 row for $DS after purge" ;;
		''|ERR|null)
			fail "purge: cannot count $_t rows for $DS"
			;;
		*)
			fail "purge: $_t still has $_n rows for $DS after purge"
			;;
		esac
	done
	_ring="$("$ZFS" events -j "$DS" 2>/dev/null || true)"
	case "$_ring" in
	""|"[]") ;;
	*) fail "purge: kernel ring not empty after purge: $_ring" ;;
	esac
	pass purge
}

step_version_refusal() {
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "version-refusal: failed to stop daemon"
	# The daemon DB is root-owned; the sqlite write needs sudo too.
	"${SUDO[@]}" python3 - "$WD/zmd.db" <<'PY'
import sqlite3
import sys

con = sqlite3.connect(sys.argv[1])
con.execute(
    "insert or replace into meta values "
    "('events_schema_version', '99')")
con.commit()
PY
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	# Anchor the journal window at the restart so the refusal-message
	# greps below cannot match output from earlier steps of this unit.
	_since="$(date '+%Y-%m-%d %H:%M:%S')"
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
	log="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager \
	    --since "$_since" 2>/dev/null || true)"
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
step_gap
step_restart_loss
step_guid_purge
step_migration
step_retention
step_purge
step_version_refusal

printf 'E2E: ALL PASS\n'
exit 0
