#!/usr/bin/env bash
#
# tests/events-schema-e2e.sh - end-to-end validation of the extended
# metadata event/schema feature chain against a live system.
#
# Chain under test: schema export (incl. clobber refusal + --force) ->
# schema check (ok + version-refusing bad file) -> wire schema_version
# presence -> zmetad daemon run -> multi-op event capture
# (CREATE/RENAME/TRUNCATE/SYMLINK) -> SQLite assertions (events,
# datasets, meta, layout) -> SIGUSR1 forced collect -> gap-row
# detection on ring wrap -> ring swap + purge -> synthetic v1/v3
# database migrations -> retention (incl. NULL captured_at) ->
# daemon version refusal on stale meta version.
#
# Usage: bash tests/events-schema-e2e.sh
# Run as root or as a user with passwordless sudo (dataset and file ops
# under /testpool are root-owned).  Exit 0 = whole chain verified;
# exit 1 = first failed assertion printed.  Idempotent: fresh dataset
# per run (e2e-<pid>), and an EXIT trap destroys this run's dataset,
# systemd unit and workdir even on failure.
#
# Both documented run modes work: the daemon creates the DB as root
# (WAL side files root-owned), so EVERY sqlite read goes through
# "${SUDO[@]}" - unprivileged when run as root, sudo when run as a
# passwordless-sudo user.
#
# Parallel-run safety: the systemd unit name and every dataset carry
# this run's ID ($$); the stale-artifact sweep in preflight matches
# ONLY this run's ID, so concurrent runs cannot destroy each other.
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
RUNID="$$"
UNIT="zmd-e2e-$RUNID"
POOL="testpool"
BASE="fs1"
BASE_DS="${POOL}/${BASE}"
DS_NAME="e2e-$RUNID"
DS="${BASE_DS}/${DS_NAME}"
SWAP_DS="${BASE_DS}/e2e-swp-$RUNID"
RET_DS="e2e-ret-$RUNID"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SCHEMA_FILE="${REPO}/contrib/zmetad/events-schema.json"

CREATED_DS=0
CREATED_SWAP_DS=0
WD=""
WIRE_HAS_VERSION=0
WIRE_VERSION=
NEW_RING_GUID=
ZMETAD="${ZMETAD:-}"
SUDO=()
if [ "$(id -u)" -ne 0 ]; then
	SUDO=(sudo)
fi

pass() { printf 'PASS: %s\n' "$1"; }
# fail() writes to stderr so its diagnostics survive command
# substitution in any caller (helpers echo machine values on stdout
# only); it still terminates the suite.
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
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
# TERM/INT must clean up too (E2E-9): an EXIT-only trap leaks the
# unit, dataset and DB when the suite is killed between steps.
# Routing the signal through exit runs the EXIT trap above.
trap 'exit 143' TERM
trap 'exit 130' INT

# db_query <db> <python-expression using con> : print the value.
# Runs privileged: the daemon creates the DB (and its -wal/-shm
# files) as root, so an unprivileged read of a root-owned WAL
# database fails and every assertion would time out (K16/T2).
db_query() {
	"${SUDO[@]}" python3 - "$1" "$2" <<'PY'
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
	"${SUDO[@]}" python3 - "$1" "$2" <<'PY'
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

# db_count <db> <dataset> <event_type> <path>: row count (stdout).
db_count() {
	db_query "$1" \
	    "con.execute('select count(*) from events where ' \
	    'dataset=? and event_type=? and path=?', \
	    ('$2', '$3', '$4')).fetchone()[0]"
}

# unit_start <interval> [extra zmetad args...]: (re)start the daemon
# unit on the suite DB and wait until it is active.
unit_start() {
	_interval="$1"
	shift
	"${SUDO[@]}" systemctl stop "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemd-run --unit="$UNIT" \
		--description="zmetad e2e validation" \
		"$ZMETAD" -f -i "$_interval" "$@" -d "$WD/zmd.db" ||
		fail "unit_start: systemd-run failed (interval=$_interval)"
	# Poll for liveness instead of a fixed sleep (E2E-12): sleep 4
	# both wasted time on fast starts and raced daemons that die
	# slowly (e.g. a DB open blocked on a stale WAL).  is-active
	# plus a live MainPID is the liveness proof; on timeout the
	# journal tail is printed so the cause of a slow failure is
	# visible.
	_up=0
	_i=0
	while [ "$_i" -lt 15 ]; do
		if "${SUDO[@]}" systemctl is-active --quiet "$UNIT" \
		    2>/dev/null; then
			_pid="$("${SUDO[@]}" systemctl show -p MainPID \
			    --value "$UNIT" 2>/dev/null || true)"
			case "$_pid" in
			''|0) ;;
			*)
				if "${SUDO[@]}" kill -0 "$_pid" 2>/dev/null; then
					_up=1
					break
				fi
				;;
			esac
		fi
		sleep 1
		_i=$((_i + 1))
	done
	if [ "$_up" -ne 1 ]; then
		_j="$("${SUDO[@]}" journalctl -u "$UNIT" --no-pager -n 10 \
		    2>/dev/null || true)"
		fail "unit_start: unit $UNIT not active within 15s; journal tail: $_j"
	fi
}

step_preflight() {
	"$ZPOOL" list -H -o name "$POOL" >/dev/null 2>&1 ||
		fail "preflight: pool $POOL not imported"

	# Stale-artifact sweep, run-ID scoped: the unit name and
	# dataset names embed $$, so the only stale artifacts this
	# may match are ones left by an earlier attempt of THIS run
	# (pid reuse after a signal).  Wildcard sweeps across other
	# runs' artifacts are deliberately not done: parallel suites
	# must not destroy each other (T6).
	"${SUDO[@]}" systemctl stop "$UNIT" >/dev/null 2>&1 || true
	"${SUDO[@]}" systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true
	while read -r d; do
		case "$d" in
		"$DS"|"$SWAP_DS")
			"${SUDO[@]}" "$ZFS" destroy -R "$d" >/dev/null 2>&1 || true ;;
		esac
	done < <("$ZFS" list -r -H -o name "$BASE_DS" 2>/dev/null)

	# Cross-run hygiene for leaked units (E2E-9): a prior suite run
	# killed by SIGKILL left its EXIT trap unrun.  A zmd-e2e-* unit
	# whose MainPID no longer exists can never clean up after
	# itself, so stop it.  LIVE units (a parallel run's daemon) are
	# left alone.
	while read -r u; do
		case "$u" in
		zmd-e2e-*) ;;
		*) continue ;;
		esac
		[ "$u" = "$UNIT" ] && continue
		_p="$("${SUDO[@]}" systemctl show -p MainPID --value \
		    "$u" 2>/dev/null || true)"
		case "$_p" in
		''|0) _dead=1 ;;
		*) if "${SUDO[@]}" kill -0 "$_p" 2>/dev/null; then
			_dead=0
		   else
			_dead=1
		   fi ;;
		esac
		if [ "$_dead" = "1" ]; then
			"${SUDO[@]}" systemctl stop "$u" >/dev/null 2>&1 || true
			"${SUDO[@]}" systemctl reset-failed "$u" \
			    >/dev/null 2>&1 || true
		fi
	done < <("${SUDO[@]}" systemctl list-units --all --no-legend \
	    'zmd-e2e-*' 2>/dev/null | awk '{print $1}' | sed 's/\.service$//')

	# Same leak class, datasets: destroy e2e-* children of $BASE_DS
	# older than this run's pid scope.  A dead unit can no longer
	# hold its dataset, and no live run owning an in-use e2e-* dataset
	# can have a pid below the current one (pids only grow between
	# wraps; a wrapped pid count of this machine makes collision
	# practically impossible for the e2e window).  This is the
	# deliberate, bounded exception to the strict run-ID scoping
	# above, and it only touches names the suites themselves mint.
	while read -r d; do
		case "$d" in
		"$BASE_DS"/e2e-*|"$BASE_DS"/io-e2e-*) ;;
		*) continue ;;
		esac
		case "$d" in
		*-"$RUNID") continue ;;  # this run's own names: handled above
		esac
		_rid="${d##*-}"
		case "$_rid" in
		''|*[!0-9]*) continue ;;
		esac
		# Never reap a name minted by a LIVE run, even when its
		# pid sorts below ours.  This suite mints one systemd unit
		# per run (zmd-e2e-<pid>), so an active unit proves the
		# run is live; the io suite mints runs with no unit, so
		# fall back to "is the pid still alive".  Only leftovers of
		# runs that are BOTH older and dead are destroyed.
		if "${SUDO[@]}" systemctl is-active --quiet \
		    "zmd-e2e-$_rid" 2>/dev/null; then
			continue
		fi
		if kill -0 "$_rid" 2>/dev/null; then
			continue
		fi
		if [ "$_rid" -lt "$RUNID" ]; then
			"${SUDO[@]}" "$ZFS" destroy -R "$d" >/dev/null 2>&1 || true
		fi
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
	elif [ -x "${REPO}/zmetad" ]; then
		ZMETAD="${REPO}/zmetad"
	elif [ -x "${REPO}/contrib/zmetad/zmetad" ]; then
		ZMETAD="${REPO}/contrib/zmetad/zmetad"
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

	# Clobber refusal: a second export over an existing file must
	# fail with rc 1 naming --force; the file must be untouched.
	err="$("$ZMETAD" --export-schema "$WD/schema.json" 2>&1 >/dev/null)"
	rc=$?
	[ "$rc" -eq 1 ] ||
		fail "export: clobbering re-export exited rc=$rc, expected 1"
	case "$err" in
	*--force*) ;;
	*) fail "export: clobber refusal does not mention --force: $err" ;;
	esac
	cmp -s "$WD/schema.json" "$SCHEMA_FILE" ||
		fail "export: refused re-export modified the file"

	# --force overwrites and the content is again canonical.
	"$ZMETAD" --export-schema "$WD/schema.json" --force ||
		fail "export: --export-schema --force failed"
	cmp -s "$WD/schema.json" "$SCHEMA_FILE" ||
		fail "export: forced re-export differs from $SCHEMA_FILE"
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
	err="$("$ZMETAD" --check-schema "$WD/bad.json" 2>&1 >/dev/null)"
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
	# E2E-5: the wire schema_version is a real kernel-to-userspace
	# contract, not advisory: the daemon stores it in meta on its
	# first poll, so an absent or wrong value is a kernel regression
	# and must fail the step.  Two things are checked:
	#   - schema_version PRESENT in the GET_EVENTS reply (it is a
	#     top-level ioctl key, which `zfs events -j` does NOT print,
	#     so the raw probe below is the only path), and
	#   - its VALUE equals the exported schema's schema_version.
	# A compile failure is a HARD failure, not a skip: the probe
	# links against the in-tree-built libzfs_core/libnvpair in
	# $REPO/.libs, which exist in any tree this suite can run
	# against (the io suite hard-fails the same way).
	probe="$WD/wireprobe.c"
	bin="$WD/wireprobe"
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
	/* Key absent from the reply: distinct from an ioctl error so
	 * the caller can fail on it (rc 3). */
	return 3;
}
EOF
	if ! gcc -I "$REPO/include" -I "$REPO/lib/libspl/include" \
	    "$probe" -o "$bin" \
	    -L "$REPO/.libs" -lzfs_core -lnvpair \
	    >"$WD/wireprobe.cc.err" 2>&1; then
		fail "module-version: gcc probe failed to compile (see $WD/wireprobe.cc.err)"
	fi
	wire="$("${SUDO[@]}" env \
	    LD_LIBRARY_PATH="$REPO/.libs" \
	    "$bin" "$BASE_DS" 2>/dev/null)"
	rc=$?
	case "$rc" in
	0) ;;
	2) fail "module-version: lzc_get_events($BASE_DS) failed (rc 2)" ;;
	3) fail "module-version: schema_version key absent from the GET_EVENTS reply - kernel wire regression" ;;
	*) fail "module-version: wire probe exited rc=$rc" ;;
	esac
	file_version="$(python3 -c \
	    'import json,sys; print(json.load(open(sys.argv[1]))["schema_version"])' \
	    "$WD/schema.json")"
	[ "$wire" = "$file_version" ] ||
		fail "module-version: wire schema_version=$wire, exported schema says $file_version"
	WIRE_HAS_VERSION=1
	WIRE_VERSION="$wire"
	pass module-version
}

step_daemon_run() {
	unit_start 3
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

	# Nested create plus a cross-directory rename. A resolver that
	# only works while the objmap graph is empty accepts CREATE 'a'
	# and then leaves a/b/c.txt NULL, and a same-directory rename
	# does not prove old_full_path used the old parent.
	"${SUDO[@]}" mkdir "$mnt/a/b" || fail "ops: mkdir a/b"
	"${SUDO[@]}" dd if=/dev/zero of="$mnt/a/b/c.txt" bs=4096 count=1 \
		status=none || fail "ops: write a/b/c.txt"
	"${SUDO[@]}" mv "$mnt/a/b/c.txt" "$mnt/a/d.txt" ||
		fail "ops: cross-dir rename a/b/c.txt -> a/d.txt"

	# Poll interval is 3s; the cross-dir rename is the last op, so
	# waiting for it also proves the earlier rows have been polled.
	found=0
	i=0
	while [ "$i" -lt 20 ]; do
		n="$(db_count "$WD/zmd.db" "$DS" RENAME d.txt 2>/dev/null || true)"
		if [ "$n" = "1" ]; then
			found=1
			break
		fi
		sleep 2
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] ||
		fail "ops: cross-dir RENAME d.txt never appeared within 40s"
	pass ops
}

step_assert() {
	# Deviations 2 and 3 applied below: dataset-relative wire names,
	# `size` column for TRUNCATE.  Reads run privileged (T2).
	"${SUDO[@]}" python3 - "$WD/zmd.db" "$DS" "$WD/schema.json" <<'PY'
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
    errors.append("meta events_schema_version key absent")
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
elif dbver[0][0] != "8":
    errors.append("meta db_schema_version=%r, expected '8'"
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

# Exact paths, not "ends with the name". A wrong parent still ends
# with the basename, and that is the bug root_objid exists to fix.
# Root object ids are not always 2, so these strings are the check.
# E2E-6: exact EQUALITY against the dataset-relative path computed
# from the known mkdir/touch sequence (not a suffix test), at two
# different depths, and each expected row must appear EXACTLY once
# (a resolver emitting a second mis-attributed row alongside the
# correct one must fail here, not hide in a list).
_exact = {
    ("CREATE", "a"): "a",                    # depth 1
    ("CREATE", "b"): "a/b",                  # depth 2
    ("CREATE", "c.txt"): "a/b/c.txt",        # depth 3
}
# d.txt has NO CREATE row: it comes into being only via the cross-dir
# RENAME asserted below, which checks its full_path exactly.
_fp_rows = con.execute(
    "select event_type, path, full_path from events where dataset=?",
    (ds,)).fetchall()
for (_t, _p), _want in _exact.items():
    _got = [r[2] for r in _fp_rows if r[0] == _t and r[1] == _p]
    if len(_got) == 0:
        errors.append("%s %r has no row (expected full_path=%r)"
                      % (_t, _p, _want))
    elif len(_got) > 1:
        errors.append("%s %r has %d rows, expected exactly 1"
                      % (_t, _p, len(_got)))
    elif _got[0] != _want:
        errors.append("%s %r full_path=%r, expected exactly %r"
                      % (_t, _p, _got[0], _want))
_xren = con.execute(
    "select full_path, old_full_path from events "
    "where dataset=? and event_type='RENAME' and path=? and old_path=?",
    (ds, "d.txt", "c.txt")).fetchall()
# E2E-6: the cross-directory RENAME's old_full_path must be EXACT
# (proving the old parent binding), not merely non-NULL.
if len(_xren) != 1:
    errors.append("cross-dir RENAME path='d.txt' old_path='c.txt': "
                  "%d rows, expected 1" % len(_xren))
elif _xren[0] != ("a/d.txt", "a/b/c.txt"):
    errors.append("cross-dir RENAME paths %r, expected exactly "
                  "('a/d.txt', 'a/b/c.txt')" % (_xren[0],))

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

# datasets table: the collect pass must have upserted this dataset's
# mountpoint row, with a '/'-prefixed mountpoint (T14).
dsrows = con.execute(
    "select mountpoint from datasets where dataset=?", (ds,)).fetchall()
if not dsrows:
    errors.append("datasets table has no row for %s" % ds)
elif not str(dsrows[0][0]).startswith("/"):
    errors.append("datasets mountpoint for %s is %r, expected a "
                  "'/'-prefixed path" % (ds, dsrows[0][0]))

if errors:
    for e in errors:
        print("ASSERT FAIL: %s" % e, file=sys.stderr)
    sys.exit(1)
PY
	[ $? -eq 0 ] || fail "assert: see ASSERT FAIL lines above"
	pass assert
}

# wait_collect_done <baseline>: poll the DB until the daemon's
# startup collect has COMPLETED.  Completion is proven, not assumed
# (E2E-7): each collect_all_events pass refreshes datasets.last_seen
# for every events-enabled dataset it walked, so once max(last_seen)
# holds still across two consecutive polls (>=2s apart, shorter than
# any sane poll interval), a collect pass has finished and the next
# one cannot start before the interval elapses.  With interval 300s
# the caller then has a wide, signal-exclusive window.
#
# <baseline> is max(last_seen) sampled BEFORE the daemon started.
# last_seen persists across restarts, so without it a row left by an
# earlier run is already "stable" and this wait would return before
# the startup collect walked anything (E2E-4).  Return only once the
# current value is strictly greater than <baseline> (0 when there is
# no row).
wait_collect_done() {
	_base="${1:-0}"
	_prev=-1
	_i=0
	while [ "$_i" -lt 30 ]; do
		_cur="$(db_query "$WD/zmd.db" \
		    "con.execute('select coalesce(max(last_seen), 0) ' \
		    'from datasets').fetchone()[0]" 2>/dev/null)"
		case "$_cur" in
		''|ERR|null|None|[!0-9]*)
			sleep 2
			_i=$((_i + 1))
			continue
			;;
		esac
		if [ "$_cur" -gt "$_base" ] && [ "$_cur" = "$_prev" ]; then
			return 0
		fi
		_prev="$_cur"
		sleep 2
		_i=$((_i + 1))
	done
	return 1
}

step_sigusr1() {
	# T5: SIGUSR1 forces an out-of-band collect.  Restart the
	# daemon on a LONG interval (300s) so any row appearing
	# quickly can only come from the signal-driven collect.
	# Snapshot the persisted last_seen BEFORE the restart so the
	# wait below cannot be satisfied by a previous run's row
	# (E2E-4); the fresh collect must move it strictly past this.
	_base="$(db_query "$WD/zmd.db" \
	    "con.execute('select coalesce(max(last_seen), 0) ' \
	    'from datasets').fetchone()[0]" 2>/dev/null)"
	case "$_base" in
	''|ERR|null|None|[!0-9]*) _base=0 ;;
	esac
	unit_start 300
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"

	# Prove the startup collect FINISHED before touching anything
	# (E2E-7): a fixed sleep cannot - a slow VM run may still be
	# inside the startup batch, which would ingest the file below
	# and make the timing proof unsound.  wait_collect_done polls
	# until datasets.last_seen stabilizes past the snapshot above,
	# i.e. the fresh startup batch ended.
	if ! wait_collect_done "$_base"; then
		fail "sigusr1: startup collect never stabilized within 60s"
	fi

	# --- variant 1: signal between polls ---
	"${SUDO[@]}" touch "$mnt/usr1" ||
		fail "sigusr1: touch usr1 failed"
	_pid="$("${SUDO[@]}" systemctl show -p MainPID --value "$UNIT" \
	    2>/dev/null || true)"
	signal_usr1() {
		if ! "${SUDO[@]}" systemctl kill -s USR1 "$UNIT" 2>/dev/null; then
			[ -n "$_pid" ] && [ "$_pid" != "0" ] ||
				fail "sigusr1: cannot determine daemon pid"
			"${SUDO[@]}" kill -USR1 "$_pid" ||
				fail "sigusr1: kill -USR1 $_pid failed"
		fi
	}
	# Signal once now, then re-signal on each poll tick: the
	# create must first reach the ring via txg sync (~5s), and a
	# signal that lands before that sync collects nothing.  The
	# schedule alone (300s) cannot produce the row inside the
	# 60s budget, so appearance proves the signal path.
	signal_usr1
	found=0
	i=0
	while [ "$i" -lt 30 ]; do
		n="$(db_count "$WD/zmd.db" "$DS" CREATE usr1 2>/dev/null)"
		[ "$n" = "1" ] && { found=1; break; }
		sleep 2
		signal_usr1
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] ||
		fail "sigusr1: usr1 CREATE row absent 60s after SIGUSR1"
	# Timing proof is the loop bound itself: the row must appear
	# within the 30*2s poll budget above, while the daemon's poll
	# interval is 300s, so a scheduled collect cannot possibly fire
	# inside that window and only the signal path can explain the
	# row.  (A separate elapsed < 120 check was tautological - it
	# could not fail once the loop's own 60s cap had passed.)

	# --- variant 2: signals during a large in-progress collect ---
	# Restart on a 15s interval so the assertion cannot be
	# starved by a swallowed signal (zmetad.c test-and-clear
	# race); the bulk ring below keeps a collect in flight while
	# the signals land.  Assert: no crash, no duplicate rows.
	unit_start 15
	"${SUDO[@]}" touch "$mnt/usr2" ||
		fail "sigusr1: touch usr2 failed"
	# Bulk metadata churn: 1500 creates land in the ring, so the
	# next collect pass is long-running.
	"${SUDO[@]}" sh -c 'for i in $(seq 0 1499); do touch "$0/bulk.$i"; done' "$mnt" ||
		fail "sigusr1: bulk create failed"
	for _s in 1 2 3; do
		"${SUDO[@]}" systemctl kill -s USR1 "$UNIT" \
		    >/dev/null 2>&1 || true
		sleep 1
	done
	"${SUDO[@]}" systemctl is-active --quiet "$UNIT" ||
		fail "sigusr1: daemon died during signaled bulk collect"
	found=0
	i=0
	while [ "$i" -lt 45 ]; do
		n="$(db_count "$WD/zmd.db" "$DS" CREATE usr2 2>/dev/null)"
		[ "$n" = "1" ] && { found=1; break; }
		[ "${n:-0}" -gt 1 ] 2>/dev/null &&
			fail "sigusr1: usr2 duplicated ($n rows) after signal storm"
		sleep 2
		i=$((i + 1))
	done
	[ "$found" -eq 1 ] ||
		fail "sigusr1: usr2 CREATE row never appeared (exactly once) within 90s"
	"${SUDO[@]}" systemctl is-active --quiet "$UNIT" ||
		fail "sigusr1: daemon not active after signal storm"
	# Dedup proof across every signaled collect: the UNIQUE key
	# means duplicates would have been silently ignored; assert
	# the bulk rows also landed exactly once each (sample the
	# last one).
	n="$(db_count "$WD/zmd.db" "$DS" CREATE bulk.1499 2>/dev/null)"
	case "$n" in
	0|"") ;;	# bulk tail may still be in flight; not fatal
	1) ;;
	*) fail "sigusr1: bulk.1499 has $n rows, expected at most 1" ;;
	esac

	# Restore the short interval the remaining steps rely on.
	unit_start 3
	pass sigusr1
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
	# Drive a REAL wrap on the DEFAULT 1 MiB ring (~10000
	# records): at window=0 (one record per write) 64MB of 4K
	# writes emits 16384 records, more than the ring holds, so
	# at least one wrap lands between two 3s polls.
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
	# Wrap while the daemon is down.  Detecting a last_lost
	# NON-persistence regression (E2E-1/GH#1): a healthy daemon
	# reads the persisted last_lost baseline (C0) at open and, on
	# its first poll after restart, reports ONLY the loss
	# accumulated during the downtime - a gap row of C1 - C0,
	# where C1 is the kernel's cumulative lost counter.  A broken
	# daemon (get_last_lost error mistaken for "no baseline")
	# re-arms and reports the FULL cumulative C1.
	#
	# Discriminator: with C0 > 0 the healthy report C1 - C0 is
	# strictly less than C1, while the re-armed build reports
	# exactly C1 and is never < C1.  The earlier "sum(lost) after
	# minus sum(lost) before" form could NOT tell the two apart: it
	# subtracted a prior loss count from a post-restart loss count,
	# and on the re-armed build that subtraction cancelled almost
	# all of the re-reported loss (C1 ~= 16.4k minus the
	# pre-restart sum ~= 15.9k ~= 500), which slipped under the
	# 16384 bound - the very failure this step exists to catch.
	local before found i _llost post_ll new_lost
	before="$(db_query "$WD/zmd.db" \
	    "con.execute('select coalesce(max(id), 0) from gaps where ' \
	    'dataset=?', ('$DS',)).fetchone()[0]" 2>/dev/null)"
	case "$before" in
	''|ERR|null|None)
		fail "restart-loss: cannot read gaps baseline"
		return
		;;
	esac
	# C0: the persisted cumulative ring-loss baseline at the moment
	# the daemon stops.  Scalar query with "or [None]" so a SQL NULL
	# in an existing row also prints as None and the guard below can
	# fire (E2E-8): a bare fetchone() yields the truthy tuple
	# (None,), which the case would not match.
	_llost="$(db_query "$WD/zmd.db" \
	    "(con.execute('select last_lost from sync_state where ' \
	    'dataset=?', ('$DS',)).fetchone() or [None])[0]" \
	    2>/dev/null)"
	case "$_llost" in
	''|ERR|null|None|[!0-9]*)
		fail "restart-loss: sync_state.last_lost=$_llost is not a number after step_gap"
		return
		;;
	esac
	# Precondition for the discriminator below: C0 must be non-zero,
	# i.e. step_gap must have produced wrap loss.  Without it the
	# healthy report would not be strictly less than C1 and the test
	# would pass vacuously on both builds - fail loudly instead.
	[ "$_llost" -gt 0 ] ||
		fail "restart-loss: pre-restart last_lost=$_llost is 0 (step_gap produced no ring loss); the re-baseline discriminator cannot fire"
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
	# Wait for the first post-restart poll(s) to land, then read the
	# new loss and the freshly persisted baseline C1.  A longer wait
	# cannot create a false PASS: the healthy build reports C1 - C0
	# once and then nothing; only a re-armed baseline keeps
	# re-reporting, and the C1 comparison below rejects it.
	found=0
	i=0
	while [ "$i" -lt 20 ]; do
		sleep 2
		new_lost="$(db_query "$WD/zmd.db" \
		    "con.execute('select coalesce(sum(lost), 0) from gaps ' \
		    'where dataset=? and id > ? and lost > 0', \
		    ('$DS', $before)).fetchone()[0]" 2>/dev/null)"
		case "$new_lost" in
		''|ERR|null|None)
			i=$((i + 1))
			continue
			;;
		esac
		[ "$new_lost" -gt 0 ] || { i=$((i + 1)); continue; }
		# C1: the cumulative ring-loss baseline the daemon
		# persisted at its first post-restart poll.  Read fresh
		# each tick and require it to have advanced past C0:
		# until that poll writes it the row still holds the
		# pre-restart value, which would make the comparison
		# below meaningless (and could fail a healthy build).
		post_ll="$(db_query "$WD/zmd.db" \
		    "(con.execute('select last_lost from sync_state where ' \
		    'dataset=?', ('$DS',)).fetchone() or [None])[0]" \
		    2>/dev/null)"
		case "$post_ll" in
		''|ERR|null|None|[!0-9]*)
			i=$((i + 1))
			continue
			;;
		esac
		[ "$post_ll" -gt "$_llost" ] ||
			{ i=$((i + 1)); continue; }
		# (b) Discriminator: the healthy build reports C1 - C0 with
		# C0 > 0, strictly less than C1; the re-armed build reports
		# the full cumulative C1 and is never < C1.  This is the
		# check the old dimensionally-wrong delta form lacked.
		#
		# No absolute bound is asserted here on purpose: the loss
		# accrued while the daemon was down is bounded by the
		# RING's record capacity, not by the number of writes
		# issued during the downtime - a bulk write that displaces
		# every record the ring held reports the full capacity
		# (observed 16386 records on an empty-then-filled ring),
		# so any "less than the write count" bound false-fails a
		# healthy build.
		if [ "$new_lost" -ge "$post_ll" ]; then
			fail "restart-loss: post-restart new loss $new_lost >= persisted cumulative last_lost $post_ll (C1) - last_lost baseline was re-armed, not persisted (C0=$_llost)"
			return
		fi
		found=1
		break
	done
	[ "$found" -eq 1 ] ||
		fail "restart-loss: no discriminating new positive gaps row after restart (before id $before, C0=$_llost)"
	"${SUDO[@]}" "$ZFS" set events_io_window=1000 "$DS" ||
		fail "restart-loss: restore events_io_window"
	pass restart-loss
}

# do_guid_swap <db> <dataset>: destroy + recreate an events=on child
# between daemon polls and wait (up to 30s, like step_gap) for the
# ring-replace evidence: a gaps row with the lost=-1 sentinel, the
# journal "ring replaced" warning, and a changed sync_state.ring_guid.
# Called directly (NOT in command substitution) so fail() diagnostics
# reach the terminal and exit the suite (T7); on success it sets the
# global NEW_RING_GUID.
do_guid_swap() {
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
		    "con.execute('select ring_guid from sync_state ' \
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
	_new_guid=""
	_i=0
	while [ "$_i" -lt 15 ]; do
		_gap="$(db_query "$_db" \
		    "con.execute('select count(*) from gaps where ' \
		    'dataset=? and lost=-1', ('$_ds',)).fetchone()[0]" \
		    2>/dev/null)"
		_new_guid="$(db_query "$_db" \
		    "con.execute('select ring_guid from sync_state ' \
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

	NEW_RING_GUID="$_new_guid"
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
	# re-stamps sync_state.ring_guid. The new guid flows into the
	# purge assertion below via NEW_RING_GUID (T7: no command
	# substitution, so fail() diagnostics are never swallowed).
	do_guid_swap "$WD/zmd.db" "$SWAP_DS"
	[ -n "$NEW_RING_GUID" ] ||
		fail "guid-purge: swap helper produced no guid"

	# --- purge: while the ring still has events, --purge must wipe
	# DB rows (events/gaps/sync_state), clear the kernel ring, exit 0.
	# E2E-4: the daemon is STOPPED first, then the purge runs, then
	# every table is asserted strictly 0 - the old tolerance-free /
	# opposite-tolerance mix between this step and step_purge was
	# flaky because a live poll could re-write sync_state inside
	# the purge->count window.  With the daemon down the one-shot
	# CLI holds the DB alone, so no lock-retry is needed and the
	# zero-row assertion is deterministic.
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "guid-purge: failed to stop daemon"
	"${SUDO[@]}" "$ZMETAD" --purge "$SWAP_DS" -d "$WD/zmd.db" ||
		fail "guid-purge: --purge $SWAP_DS exited nonzero"
	for _t in events gaps sync_state; do
		_n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from $_t where ' \
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
	"${SUDO[@]}" "$ZMETAD" --purge "$BASE_DS/no-such-e2e-$RUNID" \
	    -d "$WD/zmd.db" >/dev/null 2>&1
	[ $? -eq 2 ] ||
		fail "guid-purge: purge of nonexistent dataset did not exit 2"

	# The swap's -1 row is gone now (purge deleted it) -- and the
	# new ring's guid must still match what sync_state carried at
	# swap time, because purge also cleared sync_state: a fresh
	# poll re-stamps from the SAME kernel ring instance.  The
	# re-stamp needs a poll that reaches the identity write: an
	# empty (just-cleared) ring returns no records, so generate
	# one event to wake the collect path.  The daemon was stopped
	# for the purge (E2E-4); restart it here.
	unit_start 3
	_mnt="$("$ZFS" get -H -o value mountpoint "$SWAP_DS")"
	"${SUDO[@]}" touch "$_mnt/post-purge-$RUNID"
	_i=0
	while [ "$_i" -lt 60 ]; do
		_after="$(db_query "$WD/zmd.db" \
		    "con.execute('select ring_guid from sync_state ' \
		    'where dataset=?', ('$SWAP_DS',)).fetchone()" 2>/dev/null)"
		case "$_after" in
		"$NEW_RING_GUID") _i=99; break ;;
		esac
		sleep 2
		_i=$((_i + 1))
	done
	[ "$_i" -eq 99 ] ||
		fail "guid-purge: sync_state.ring_guid not re-stamped to $NEW_RING_GUID within 120s"

	"${SUDO[@]}" "$ZFS" destroy -R "$SWAP_DS" || true
	CREATED_SWAP_DS=0

	pass guid-purge
}

# build_legacy_db <db> <layout>: create a synthetic pre-v4 database.
# layout=v1: events WITHOUT the 5 v2 columns, sync_state WITHOUT
# ring_guid, NO db_schema_version meta key (the ENOENT/v1 detection
# path).  layout=v3: events WITH the v2 columns but WITHOUT
# captured_at, sync_state WITH ring_guid, db_schema_version='3'
# (exercises the captured_at stage).  Both get one real events row
# and one sync_state row so the migration can be proven to preserve
# data and leave the new columns NULL.
build_legacy_db() {
	"${SUDO[@]}" python3 - "$1" "$2" "$RET_DS" <<'PY'
import sqlite3
import sys

db, layout, ds = sys.argv[1], sys.argv[2], sys.argv[3]
con = sqlite3.connect(db)

core = ("id INTEGER PRIMARY KEY AUTOINCREMENT, dataset TEXT NOT NULL, "
        "txg INTEGER NOT NULL, timestamp INTEGER NOT NULL, "
        "object_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
        "path TEXT, old_path TEXT, uid INTEGER, gid INTEGER, "
        "mode INTEGER, size INTEGER, io_offset INTEGER, io_bytes INTEGER")
v2cols = ", parent INTEGER, old_parent INTEGER, target TEXT, " \
         "old_size INTEGER, attrs INTEGER"

if layout == "v1":
    con.execute("CREATE TABLE events (%s)" % core)
    con.execute("CREATE TABLE sync_state (dataset TEXT PRIMARY KEY, "
                "last_offset INTEGER NOT NULL, last_sync INTEGER NOT NULL)")
elif layout == "v3":
    con.execute("CREATE TABLE events (%s%s)" % (core, v2cols))
    con.execute("CREATE TABLE sync_state (dataset TEXT PRIMARY KEY, "
                "last_offset INTEGER NOT NULL, last_sync INTEGER NOT NULL, "
                "ring_guid INTEGER)")
    con.execute("INSERT INTO sync_state (dataset, ring_guid, "
                "last_offset, last_sync) VALUES (?, NULL, 1, 0)",
                (ds + "-x",))
elif layout == "v4":
    con.execute("CREATE TABLE events (%s%s, captured_at INTEGER)"
                % (core, v2cols))
    con.execute("CREATE TABLE sync_state (dataset TEXT PRIMARY KEY, "
                "last_offset INTEGER NOT NULL, last_sync INTEGER NOT NULL, "
                "ring_guid INTEGER)")
else:
    raise SystemExit("bad layout " + layout)
con.execute("CREATE TABLE meta (key TEXT PRIMARY KEY, "
            "value TEXT NOT NULL)")
if layout == "v3":
    con.execute("INSERT INTO meta VALUES ('db_schema_version', '3')")
elif layout == "v4":
    con.execute("INSERT INTO meta VALUES ('db_schema_version', '4')")

if layout == "v1":
    con.execute("INSERT INTO events (dataset, txg, timestamp, object_id, "
                "event_type, path, uid, gid) VALUES (?,?,?,?,?,?,?,?)",
                (ds, 100, 12345, 42, "CREATE", "old.txt", 7, 7))
else:
    con.execute("INSERT INTO events (dataset, txg, timestamp, object_id, "
                "event_type, path, uid, gid, parent) "
                "VALUES (?,?,?,?,?,?,?,?,?)",
                (ds, 100, 12345, 42, "CREATE", "old.txt", 7, 7, 4))
con.execute("INSERT INTO sync_state (dataset, last_offset, last_sync) "
            "VALUES (?,?,?)", (ds, 512, 1700000000))
con.commit()
PY
}

# check_migrated_db <db> <layout>: post-migration invariants.
check_migrated_db() {
	"${SUDO[@]}" python3 - "$1" "$2" "$RET_DS" <<'PY'
import sqlite3
import sys

db, layout, ds = sys.argv[1], sys.argv[2], sys.argv[3]
con = sqlite3.connect("file:%s?mode=ro" % db, uri=True, timeout=5)
errors = []

ev_cols = [r[1] for r in con.execute("pragma table_info(events)")]
for col in ("parent", "old_parent", "target", "old_size", "attrs",
            "captured_at", "full_path", "old_full_path"):
    if col not in ev_cols:
        errors.append("events missing column %s after migration" % col)
sync_cols = [r[1] for r in con.execute("pragma table_info(sync_state)")]
if "ring_guid" not in sync_cols:
    errors.append("sync_state missing ring_guid after migration")
if "last_lost" not in sync_cols:
    errors.append("sync_state missing last_lost after migration")
tbls = [r[0] for r in con.execute(
    "select name from sqlite_master where type='table'")]
if "objmap" not in tbls:
    errors.append("objmap table missing after migration")

ver = con.execute("select value from meta "
                  "where key='db_schema_version'").fetchall()
if not ver or ver[0][0] != "8":
    errors.append("db_schema_version=%r, expected '8'"
                  % (ver[0][0] if ver else None,))

# DB layout 7: sync_state gains root_id (persisted root object id).
# The synthetic legacy FIXTURES must NOT have it (they model the old
# layouts), so root_id is expected here only AFTER migration.
if "root_id" not in sync_cols:
    errors.append("sync_state missing root_id after migration")

row = con.execute("select txg, timestamp, object_id, event_type, path, "
                  "uid, gid, parent, old_parent, target, old_size, "
                  "attrs, captured_at from events where dataset=?",
                  (ds,)).fetchall()
if len(row) != 1:
    errors.append("pre-existing events row lost: %d rows" % len(row))
else:
    r = row[0]
    if (r[0], r[1], r[2], r[3], r[4], r[5], r[6]) != \
            (100, 12345, 42, "CREATE", "old.txt", 7, 7):
        errors.append("pre-existing row data changed: %r" % (r[:7],))
    if layout == "v1":
        # All five v2 columns plus captured_at were added by the
        # migration and must be NULL for the old row.
        if any(v is not None for v in r[7:]):
            errors.append("added columns not NULL on old row: %r"
                          % (r[7:],))
    else:
        if r[7] != 4:
            errors.append("v3 row parent=%r, expected 4" % (r[7],))
        if any(v is not None for v in r[8:]):
            errors.append("added columns not NULL on old v3 row: %r"
                          % (r[8:],))

    # The migration chain (v1 -> ... -> 8) rebuilds sync_state from
    # the legacy fixture; assert the migrated row gained the v7
    # root_id column (NULL for a fixture that never sent root_objid)
    # and kept its data.
    sync7 = con.execute(
        "select last_offset, last_sync, root_id from sync_state "
        "where dataset=?", (ds,)).fetchall()
    if not sync7 or sync7[0][:2] != (512, 1700000000):
        errors.append("sync_state row lost or changed: %r" % (sync7,))
    elif len(sync7[0]) < 3:
        errors.append("sync_state missing root_id column after migration")
    elif sync7[0][2] is not None:
        errors.append("sync_state.root_id=%r on migrated legacy row, "
                      "expected NULL" % (sync7[0][2],))

if errors:
    for e in errors:
        print("MIGRATION FAIL: %s" % e, file=sys.stderr)
    sys.exit(1)
PY
}

step_migration() {
	# T4: REAL migration coverage on synthetic legacy databases
	# (a v1-layout DB entering the no-db_schema_version detection
	# path, a v3-layout DB exercising the captured_at stage, and
	# a v4-layout DB exercising the full_path/objmap stage),
	# driven through zmetad's one-shot open path: `--purge
	# <nonexistent>` runs zmetad_db_open (which performs the full
	# staged migration) and then exits 2 before touching the
	# kernel or any real dataset.  The running suite daemon is
	# not involved; its DB is untouched.
	for layout in v1 v3 v4; do
		mdb="$WD/mig-$layout.db"
		rm -f "$mdb" "$mdb-wal" "$mdb-shm"
		build_legacy_db "$mdb" "$layout" ||
			fail "migration: cannot build synthetic $layout database"
		"${SUDO[@]}" "$ZMETAD" --purge \
		    "$BASE_DS/no-such-mig-$RUNID-$layout" -d "$mdb" \
		    >/dev/null 2>"$WD/mig-$layout.err"
		rc=$?
		[ "$rc" -eq 2 ] ||
			fail "migration($layout): open path exited rc=$rc, expected 2 (unknown dataset)"
		check_migrated_db "$mdb" "$layout" ||
			fail "migration($layout): see MIGRATION FAIL lines above"
	done
	pass migration
}

step_retention() {
	# Gaps-retention guarantee: events captured_at older than the
	# retention window are deleted; events rows with a NULL
	# captured_at (pre-v4 legacy rows) are NEVER deleted
	# regardless of age; gaps rows are NEVER touched by cleanup
	# (only --purge removes them, per SCHEMA.md).  The daemon
	# runs its cleanup pass on the first loop tick, so a restart
	# with -r 1 exercises it without waiting a day.
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "retention: failed to stop daemon"

	# Synthetic history, all scoped to a dataset name that never
	# exists in the kernel (written straight into the suite DB):
	# an old events row (captured_at = now-7d) that the 1-day
	# window must delete, an old row with NULL captured_at that
	# must SURVIVE (T14), plus old/recent/-1 gaps rows that must
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
    "insert into events (dataset, txg, timestamp, object_id, "
    "event_type, captured_at) values (?,?,?,?,?,NULL)",
    (ds, 2, now - 30 * 86400, 2, "WRITE"))
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

	unit_start 3 -r 1

	# The cleanup pass runs on the daemon's first loop tick, AFTER
	# the first collect finishes - and a collect can take tens of
	# seconds on a dataset with a large kernel ring. Poll for the
	# deletion instead of sleeping a fixed interval (E2E-13:
	# 30x2s was marginal on a loaded VM; 60x2s stays bounded).
	_ev=""
	for _i in $(seq 1 60); do
		_ev="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from events where ' \
		    'dataset=? and captured_at is not null', \
		    ('$RET_DS',)).fetchone()[0]" 2>/dev/null)"
		[ "$_ev" = "0" ] && break
		sleep 2
	done
	[ "$_ev" = "0" ] ||
		fail "retention: old events row survived 1d retention (count=$_ev)"
	_null_ev="$(db_query "$WD/zmd.db" \
	    "con.execute('select count(*) from events where ' \
	    'dataset=? and captured_at is null', \
	    ('$RET_DS',)).fetchone()[0]" 2>/dev/null)"
	[ "$_null_ev" = "1" ] ||
		fail "retention: NULL captured_at row was deleted (count=$_null_ev, expected 1)"
	_gap_lost="$(db_query "$WD/zmd.db" \
	    "sorted(r[0] for r in con.execute('select lost from gaps ' \
	    'where dataset=?', ('$RET_DS',)).fetchall())" 2>/dev/null)"
	[ "$_gap_lost" = "[-1, 0, 7]" ] ||
		fail "retention: gaps rows not intact, lost values=$_gap_lost (expected [-1, 0, 7])"

	# Restore the daemon without retention for the final steps.
	unit_start 3
	pass retention
}

step_purge() {
	# One-shot purge of the suite dataset (holding history for it
	# from step_ops/step_gap): rc 0, kernel ring cleared, and all
	# three tables strictly 0 rows for $DS (E2E-3: no tolerance.
	# The daemon is stopped first so a concurrent poll cannot
	# re-write sync_state inside the purge->count window; a live
	# poll was the old excuse for a sync_state<=1 tolerance, which
	# could mask a real purge regression).  The one-shot CLI opens
	# the same DB file, so no lock-retry is needed once the daemon
	# is down.
	"${SUDO[@]}" systemctl stop "$UNIT" ||
		fail "purge: failed to stop daemon"
	"${SUDO[@]}" "$ZMETAD" --purge "$DS" -d "$WD/zmd.db" ||
		fail "purge: --purge $DS exited nonzero"
	for _t in events gaps sync_state; do
		_n="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from $_t where ' \
		    'dataset=?', ('$DS',)).fetchone()[0]" 2>/dev/null)"
		case "$_n" in
		0) ;;
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
	# The final step needs a RUNNING unit; bring it back (the
	# version-refusal step stops it again itself).
	unit_start 3
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
	# No unit_start here: the daemon is EXPECTED to die at open
	# (version refusal), so the is-active assertion must not run.
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


step_principal() {
	# ZFS_EV_PRINCIPAL: a process registers an opaque u64 tag via
	# ZFS_IOC_SET_PRINCIPAL; every event record it triggers carries
	# it; unregistered writers produce records with no principal key
	# (NULL column - never fabricated). Registration is a CLAIM, not
	# kernel-verified identity.
	probe="$WD/principalprobe.c"
	bin="$WD/principalprobe"
	cat > "$probe" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <libzfs/libzfs_core.h>
/* argv: [0] tagfile-mode "write <path>" | "clear" */
int main(int argc, char **argv) {
	uint64_t gen = 0;
	FILE *f;
	libzfs_core_init();
	if (argc > 2 && strcmp(argv[1], "write") == 0) {
		if (lzc_set_principal(0xDEADBEEFULL, &gen) != 0)
			return (2);
		f = fopen(argv[2], "w");
		if (f == NULL)
			return (3);
		fputs("principal\n", f);
		fclose(f);
		return (0);
	}
	if (argc > 1 && strcmp(argv[1], "clear") == 0)
		return (lzc_clear_principal(&gen) == 0 ? 0 : 1);
	return (1);
}
EOF
	if ! gcc -I "$REPO/include" -I "$REPO/lib/libspl/include" \
	    "$probe" -o "$bin" \
	    -L "$REPO/.libs" -lzfs_core -lnvpair \
	    >"$WD/principalprobe.cc.err" 2>&1; then
		fail "principal: probe compile failed (see $WD/principalprobe.cc.err)"
		return
	fi
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"
	# Registered writer: register AND write in the SAME process -
	# the table is keyed by thread group, so the writer must be the
	# registrant (sudo sh is a different, unregistered process).
	"${SUDO[@]}" env LD_LIBRARY_PATH="$REPO/.libs" \
	    "$bin" write "$mnt/pprincipal.txt" >/dev/null 2>&1 ||
		fail "principal: registered write failed"
	# Unregistered writer: another process, no registration.
	"${SUDO[@]}" sh -c "echo no-principal > '$mnt/nprincipal.txt'" ||
		fail "principal: unregistered write failed"
	"${SUDO[@]}" env LD_LIBRARY_PATH="$REPO/.libs" \
	    "$bin" clear >/dev/null 2>&1 || true
	found_reg=0; found_unreg=0; found_create=0; i=0
	while [ "$i" -lt 20 ]; do
		sleep 2
		# The tag rides EVERY record the registrant triggers, not
		# only its IO: key the wait on the tag itself (the WRITE
		# record carries the object id, not the path) and then pin
		# the CHECK below on a lifecycle record, whose attribution
		# a "some record carries the tag" check cannot see.
		reg="$(db_query "$WD/zmd.db" \
		    "con.execute('select principal from events where ' \
		    'dataset=? and principal is not null limit 1', \
		    ('$DS',)).fetchone()[0]" 2>/dev/null)"
		[ "$reg" != "3735928559" ] && { i=$((i + 1)); continue; }
		found_reg=1
		# CREATE (a lifecycle op) from the registered process must
		# carry the tag too: this is the assertion that catches a
		# build where only the WRITE/READ emitters attach it.
		create_reg="$(db_query "$WD/zmd.db" \
		    "con.execute('select principal from events where ' \
		    'dataset=? and event_type=? and path=? limit 1', \
		    ('$DS', 'CREATE', 'pprincipal.txt')).fetchone()[0]" \
		    2>/dev/null)"
		[ "$create_reg" = "3735928559" ] && found_create=1
		# The unregistered writer's records must carry no
		# principal - absence is never fabricated. Every
		# principal-bearing record in this dataset must carry the
		# registered tag; any other value means attribution
		# crossed processes.
		unreg="$(db_query "$WD/zmd.db" \
		    "con.execute('select count(*) from events where ' \
		    'dataset=? and principal is not null and ' \
		    'principal != 3735928559', ('$DS',)).fetchone()[0]" \
		    2>/dev/null)"
		[ "$unreg" = "0" ] && found_unreg=1
		break
	done
	[ "$found_reg" = "1" ] ||
		fail "principal: no record with principal=0xDEADBEEF for pprincipal.txt"
	[ "$found_unreg" = "1" ] ||
		fail "principal: unregistered writer's record carries a principal (fabricated)"
	# The lifecycle assertion is what makes this step meaningful:
	# without it a build that attaches the tag only on the IO path
	# passes on the WRITE record alone (observed: CREATE had NULL).
	[ "$found_create" = "1" ] ||
		fail "principal: registered writer's CREATE record for pprincipal.txt carries no principal (tag attached to IO records only?)"
	pass principal
}

step_root_objid() {
	# GET_EVENTS must return the mounted dataset's root object id.
	# It is not always 2; compare it to the mountpoint inode, which
	# is z_root. Absent or mismatched means the resolver cannot
	# tell "ancestor is the root" from "ancestor was lost".
	probe="$WD/rootprobe.c"
	bin="$WD/rootprobe"
	cat > "$probe" <<'EOF'
#include <stdio.h>
#include <libzfs/sys/nvpair.h>
#include <libzfs/libzfs_core.h>
int main(int argc, char **argv) {
	nvlist_t *page = NULL;
	uint64_t v = 0;
	if (argc < 2)
		return 1;
	libzfs_core_init();
	if (lzc_get_events(argv[1], 0, 0, &page) != 0)
		return 2;
	if (nvlist_lookup_uint64(page, "root_objid", &v) != 0 || v == 0)
		return 3;
	printf("%llu\n", (unsigned long long)v);
	return 0;
}
EOF
	if ! gcc -I "$REPO/include" -I "$REPO/lib/libspl/include" \
	    "$probe" -o "$bin" \
	    -L "$REPO/.libs" -lzfs_core -lnvpair \
	    >/dev/null 2>"$WD/rootprobe.err"; then
		fail "root-objid: wire probe compile failed ($(tr '\n' ' ' < "$WD/rootprobe.err"))"
	fi
	mnt="$("$ZFS" get -H -o value mountpoint "$DS")"
	ino="$("${SUDO[@]}" stat -c %i "$mnt")"
	wire="$("${SUDO[@]}" env \
	    LD_LIBRARY_PATH="$REPO/.libs" \
	    "$bin" "$DS" 2>"$WD/rootprobe.run" || true)"
	[ -n "$wire" ] ||
		fail "root-objid: GET_EVENTS did not return root_objid (rc probe, stderr=$(tr '\n' ' ' < "$WD/rootprobe.run"))"
	[ "$wire" = "$ino" ] ||
		fail "root-objid: wire=$wire mountpoint inode=$ino"
	pass root-objid
}

step_preflight
step_export
step_check_ok
step_check_bad
step_module_version
step_daemon_run
step_ops
step_root_objid
step_principal
step_assert
step_sigusr1
step_gap
step_restart_loss
step_guid_purge
step_migration
step_retention
step_purge
step_version_refusal

printf 'E2E: ALL PASS\n'
exit 0
