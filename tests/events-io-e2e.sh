#!/usr/bin/env bash
#
# tests/events-io-e2e.sh - end-to-end validation of the IO event
# feature against a live system running the leaf-01 kernel.
#
# All record assertions go through a compiled wire probe (the
# lzc_get_events ioctl), never through `zfs events` text output or
# the zmetad database: this is wire-level behavior testing.  Records
# land in the ring after txg sync (~5s default), so every presence
# check polls up to ~30s; every absence check waits a 10s grace
# period first.
#
# Assertions (each prints PASS/FAIL):
#   preflight          pool imported, work datasets + wire probe ready
#   write-visible      4096-byte write -> exactly one WRITE record,
#                      io_bytes=4096, uid/gid of the writing user
#   read-visible       read back -> exactly one READ record,
#                      io_bytes=4096
#   gate-off           events_io=off dataset emits ZERO IO records
#                      (dataset-scoped isolation; CREATE proves the
#                      ring itself is live)
#   zero-byte          truncate-to-zero create + EOF read -> no
#                      WRITE/READ records
#   fence-coalesce     window=2000: two writes 1s apart, close ->
#                      ONE merged WRITE record, io_bytes=8192
#   fence-disabled     window=0: 3 writes -> exactly 3 records
#   flush-order        window=2000: a still-pending WRITE is flushed
#                      before the RENAME, so WRITE precedes RENAME
#   close-flush        close(2) flushes a young window while another
#                      fd holds the inode, so inactive cannot be the
#                      emitter
#   byte-completeness  10 x 1KB writes in one window -> one record,
#                      io_bytes=10240
#   cleanup            EXIT trap destroys datasets + workdir
#
# Usage: bash tests/events-io-e2e.sh  (root or passwordless sudo)
# Exit 0 = every assertion passed; nonzero = one or more failed
# (all assertions run; failures are collected, not fatal).
#
# Environment overrides: ZFS, ZPOOL, ZIO_E2E_KEEP (keep workdir).

set -u

ZFS="${ZFS:-/usr/local/sbin/zfs}"
ZPOOL="${ZPOOL:-/usr/local/sbin/zpool}"
POOL="testpool"
BASE_DS="$POOL/fs1"
DS1="$BASE_DS/io-e2e-$$"
DS2="$BASE_DS/io-e2e-off-$$"
DS3="$BASE_DS/io-e2e-fence-$$"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

# Wire op codes (include/sys/zfs_events.h ZFS_EV_*).
OP_CREATE=1
OP_RENAME=3
OP_WRITE=8
OP_READ=9

SUDO=()
if [ "$(id -u)" -ne 0 ]; then
	SUDO=(sudo)
fi

CREATED1=0
CREATED2=0
CREATED3=0
WD=""
PROBE_OK=0
RECS=""
FAILS=0

pass() { printf 'PASS: %s\n' "$1"; }
notice() { printf 'NOTICE: %s\n' "$1"; }
fail() {
	FAILS=$((FAILS + 1))
	printf 'FAIL: %s\n' "$1"
}

cleanup() {
	status=$?
	ok=1
	if [ "$CREATED1" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS1" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS1"; ok=0; }
	fi
	if [ "$CREATED2" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS2" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS2"; ok=0; }
	fi
	if [ "$CREATED3" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS3" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS3"; ok=0; }
	fi
	if [ -n "$WD" ] && [ "${ZIO_E2E_KEEP:-0}" != "1" ]; then
		rm -rf "$WD"
	elif [ -n "$WD" ]; then
		notice "keeping workdir $WD (ZIO_E2E_KEEP=1)"
	fi
	if [ "$ok" -eq 1 ]; then
		printf 'PASS: cleanup\n'
	else
		status=1
	fi
	exit "$status"
}
trap cleanup EXIT

ds_mnt() {
	"$ZFS" get -H -o value mountpoint "$1"
}

# run_probe <dataset> <object-id>: dump that object's records (ring
# order) to $RECS.  Empty object id 0 means "all objects".
run_probe() {
	RECS="$WD/recs.txt"
	"${SUDO[@]}" env \
	    LD_LIBRARY_PATH="$REPO/lib/libzfs_core/.libs:$REPO/lib/libnvpair/.libs" \
	    "$WD/probe" "$1" "${2:-0}" >"$RECS" 2>/dev/null
}

# count_op <op-code>: number of records with that op in $RECS.
count_op() {
	grep -c "^REC op=$1 " "$RECS" || true
}

# poll_op <dataset> <object> <op> <n>: poll up to ~30s (txg sync is
# ~5s) until at least <n> records with <op> exist for <object>.
poll_op() {
	i=0
	while [ "$i" -lt 15 ]; do
		run_probe "$1" "$2"
		c="$(count_op "$3")"
		[ "$c" -ge "$4" ] && return 0
		sleep 2
		i=$((i + 1))
	done
	return 1
}

# grace: fixed wait so an ABSENCE assertion is not vacuous (covers
# two txg sync intervals).
grace() {
	sleep 5
	sleep 5
}

# field <line> <key>: value of key= in a REC line.
field() {
	tok="${2#=}"
	for t in $1; do
		case "$t" in
		"$2="*) printf '%s\n' "${t#*=}"; return ;;
		esac
	done
}

step_preflight() {
	"$ZPOOL" list -H -o name "$POOL" >/dev/null 2>&1 || {
		fail "preflight: pool $POOL not imported"
		return
	}

	# Destroy stale datasets from interrupted earlier runs (never
	# touch anything else under fs1).
	while read -r d; do
		case "$d" in
		*/io-e2e-*)
			"${SUDO[@]}" "$ZFS" destroy -R "$d" \
			    >/dev/null 2>&1 || true
			;;
		esac
	done < <("$ZFS" list -r -H -o name "$BASE_DS" 2>/dev/null)

	"${SUDO[@]}" "$ZFS" create "$DS1" || {
		fail "preflight: create $DS1"
		return
	}
	CREATED1=1
	"${SUDO[@]}" "$ZFS" create "$DS2" || {
		fail "preflight: create $DS2"
		return
	}
	CREATED2=1
	"${SUDO[@]}" "$ZFS" create "$DS3" || {
		fail "preflight: create $DS3"
		return
	}
	CREATED3=1

	"${SUDO[@]}" "$ZFS" set events=on events_io=on \
	    events_io_window=0 "$DS1" ||
		{ fail "preflight: props $DS1"; return; }
	"${SUDO[@]}" "$ZFS" set events=on events_io=off "$DS2" ||
		{ fail "preflight: props $DS2"; return; }
	"${SUDO[@]}" "$ZFS" set events=on events_io=on \
	    events_io_window=2000 "$DS3" ||
		{ fail "preflight: props $DS3"; return; }
	w="$(ds_mnt "$DS1")"
	case "$w" in
	/*) ;;
	*) fail "preflight: unexpected mountpoint '$w'"; return ;;
	esac

	WD="$(mktemp -d /tmp/zio-e2e.XXXXXX)"

	# Wire probe: lzc_get_events(ds, object, offset) with full
	# pagination (pattern: cmd/zfs/zfs_main.c zfs_do_events).
	probe_c="$WD/probe.c"
	cat > "$probe_c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <libzfs/sys/nvpair.h>
#include <libzfs/libzfs_core.h>

int
main(int argc, char **argv)
{
	uint64_t next = 0, prev = 0, lost = 0, obj = 0;
	int n = 0;

	if (argc < 2)
		return (2);
	if (argc > 2)
		obj = strtoull(argv[2], NULL, 0);
	libzfs_core_init();
	for (;;) {
		nvlist_t *page = NULL;
		nvlist_t *events;
		nvpair_t *pair = NULL;

		if (lzc_get_events(argv[1], obj, next, &page) != 0)
			return (2);
		if (n > 0 && next == prev)
			break;
		prev = next;
		if (nvlist_lookup_nvlist(page, "events", &events) != 0) {
			nvlist_free(page);
			break;
		}
		while ((pair = nvlist_next_nvpair(events, pair)) != NULL) {
			nvlist_t *e;
			uint16_t op = 0;
			uint64_t o = 0, txg = 0, off = 0, bytes = 0;
			uint64_t uid = 0, gid = 0;
			const char *name = NULL;

			if (nvpair_value_nvlist(pair, &e) != 0)
				continue;
			(void) nvlist_lookup_uint16(e, "op", &op);
			(void) nvlist_lookup_uint64(e, "object", &o);
			(void) nvlist_lookup_uint64(e, "txg", &txg);
			(void) nvlist_lookup_uint64(e, "io_offset", &off);
			(void) nvlist_lookup_uint64(e, "io_bytes", &bytes);
			(void) nvlist_lookup_uint64(e, "uid", &uid);
			(void) nvlist_lookup_uint64(e, "gid", &gid);
			(void) nvlist_lookup_string(e, "name", &name);
			(void) printf(
			    "REC op=%u object=%llu txg=%llu "
			    "io_offset=%llu io_bytes=%llu uid=%llu "
			    "gid=%llu name=%s\n",
			    op, (unsigned long long)o,
			    (unsigned long long)txg,
			    (unsigned long long)off,
			    (unsigned long long)bytes,
			    (unsigned long long)uid,
			    (unsigned long long)gid,
			    name ? name : "-");
			n++;
		}
		(void) nvlist_lookup_uint64(page, "next_offset", &next);
		(void) nvlist_lookup_uint64(page, "records_lost", &lost);
		nvlist_free(page);
		if (next == 0)
			break;
	}
	(void) printf("META next=%llu lost=%llu n=%d\n",
	    (unsigned long long)next, (unsigned long long)lost, n);
	return (0);
}
EOF
	if gcc -I "$REPO/include" -I "$REPO/lib/libspl/include" \
	    "$probe_c" -o "$WD/probe" \
	    -L "$REPO/lib/libzfs_core/.libs" -lzfs_core \
	    -L "$REPO/lib/libnvpair/.libs" -lnvpair \
	    >"$WD/gcc.log" 2>&1; then
		PROBE_OK=1
	else
		fail "preflight: wire probe compile failed (see \
$WD/gcc.log)"
		return
	fi
	pass preflight
}

# require_probe: record-dependent steps are unusable without the
# probe; fail fast instead of burning 30s poll timeouts per step.
require_probe() {
	if [ "$PROBE_OK" -ne 1 ]; then
		fail "$1: no wire probe"
		return 1
	fi
	return 0
}

step_write_visible() {
	require_probe write-visible || return
	mnt="$(ds_mnt "$DS1")"
	"${SUDO[@]}" chmod 0777 "$mnt"
	WRITER=""
	for u in nobody daemon; do
		if id -u "$u" >/dev/null 2>&1; then
			WRITER="$u"
			break
		fi
	done
	if [ -z "$WRITER" ]; then
		fail "write-visible: no unprivileged user found"
		return
	fi
	uid="$(id -u "$WRITER")"
	gid="$(id -g "$WRITER")"
	f="$mnt/w.bin"
	"${SUDO[@]}" -u "$WRITER" dd if=/dev/zero of="$f" bs=4096 \
	    count=1 status=none || {
		fail "write-visible: dd as $WRITER failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	if ! poll_op "$DS1" "$obj" "$OP_WRITE" 1; then
		fail "write-visible: no WRITE record within 30s"
		return
	fi
	n="$(count_op "$OP_WRITE")"
	[ "$n" -eq 1 ] ||
		fail "write-visible: expected 1 WRITE record, got $n"
	line="$(grep "^REC op=$OP_WRITE " "$RECS" | head -n 1)"
	ib="$(field "$line" io_bytes)"
	u="$(field "$line" uid)"
	g="$(field "$line" gid)"
	[ "$ib" = "4096" ] ||
		fail "write-visible: io_bytes=$ib, expected 4096"
	[ "$u" = "$uid" ] ||
		fail "write-visible: uid=$u, expected $uid ($WRITER)"
	[ "$g" = "$gid" ] ||
		fail "write-visible: gid=$g, expected $gid ($WRITER)"
	[ "$FAILS" -eq "$f0" ] && pass write-visible
}

step_read_visible() {
	require_probe read-visible || return
	mnt="$(ds_mnt "$DS1")"
	f="$mnt/w.bin"
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	WRITER=""
	for u in nobody daemon; do
		if id -u "$u" >/dev/null 2>&1; then
			WRITER="$u"
			break
		fi
	done
	f0="$FAILS"
	"${SUDO[@]}" -u "$WRITER" dd if="$f" of=/dev/null bs=4096 \
	    count=1 status=none || {
		fail "read-visible: dd read failed"
		return
	}
	if ! poll_op "$DS1" "$obj" "$OP_READ" 1; then
		fail "read-visible: no READ record within 30s"
		return
	fi
	n="$(count_op "$OP_READ")"
	[ "$n" -eq 1 ] ||
		fail "read-visible: expected 1 READ record, got $n"
	line="$(grep "^REC op=$OP_READ " "$RECS" | head -n 1)"
	ib="$(field "$line" io_bytes)"
	[ "$ib" = "4096" ] ||
		fail "read-visible: io_bytes=$ib, expected 4096"
	[ "$FAILS" -eq "$f0" ] && pass read-visible
}

step_gate_off() {
	require_probe gate-off || return
	mnt="$(ds_mnt "$DS2")"
	f="$mnt/g.bin"
	"${SUDO[@]}" dd if=/dev/zero of="$f" bs=4096 count=1 \
	    status=none || { fail "gate-off: write failed"; return; }
	"${SUDO[@]}" dd if="$f" of=/dev/null bs=4096 count=1 \
	    status=none || { fail "gate-off: read failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	grace
	run_probe "$DS2" "$obj"
	f0="$FAILS"
	nw="$(count_op "$OP_WRITE")"
	nr="$(count_op "$OP_READ")"
	nc="$(count_op "$OP_CREATE")"
	[ "$nc" -ge 1 ] ||
		fail "gate-off: CREATE record missing; ring is not \
capturing this dataset at all, so the IO absence below is vacuous"
	[ "$nw" -eq 0 ] ||
		fail "gate-off: $nw WRITE records with events_io=off"
	[ "$nr" -eq 0 ] ||
		fail "gate-off: $nr READ records with events_io=off"
	[ "$FAILS" -eq "$f0" ] && pass gate-off
}

step_zero_byte() {
	require_probe zero-byte || return
	mnt="$(ds_mnt "$DS1")"
	f="$mnt/z.bin"
	"${SUDO[@]}" sh -c ": > '$f'" ||
		{ fail "zero-byte: truncate-create failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	# EOF read: 0 bytes transferred must not emit a READ record.
	"${SUDO[@]}" dd if="$f" of=/dev/null bs=4096 count=1 \
	    status=none || { fail "zero-byte: EOF read failed"; return; }
	grace
	run_probe "$DS1" "$obj"
	f0="$FAILS"
	nw="$(count_op "$OP_WRITE")"
	nr="$(count_op "$OP_READ")"
	nc="$(count_op "$OP_CREATE")"
	[ "$nc" -ge 1 ] ||
		fail "zero-byte: CREATE record missing; absence checks \
below are vacuous"
	[ "$nw" -eq 0 ] ||
		fail "zero-byte: $nw WRITE records for a zero-byte file"
	[ "$nr" -eq 0 ] ||
		fail "zero-byte: $nr READ records for an EOF-only read"
	[ "$FAILS" -eq "$f0" ] && pass zero-byte
}

step_fence_coalesce() {
	require_probe fence-coalesce || return
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/c.bin"
	"${SUDO[@]}" python3 - "$f" <<'PY'
import os
import sys
import time

fd = os.open(sys.argv[1], os.O_CREAT | os.O_WRONLY | os.O_TRUNC,
             0o644)
os.write(fd, b"x" * 4096)
time.sleep(1.0)
os.write(fd, b"x" * 4096)
os.close(fd)
PY
	[ $? -eq 0 ] || {
		fail "fence-coalesce: timed writes failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	if ! poll_op "$DS3" "$obj" "$OP_WRITE" 1; then
		fail "fence-coalesce: no WRITE record within 30s"
		return
	fi
	n="$(count_op "$OP_WRITE")"
	[ "$n" -eq 1 ] || fail "fence-coalesce: expected 1 merged \
WRITE record, got $n (window did not coalesce)"
	line="$(grep "^REC op=$OP_WRITE " "$RECS" | head -n 1)"
	ib="$(field "$line" io_bytes)"
	off="$(field "$line" io_offset)"
	[ "$ib" = "8192" ] || fail "fence-coalesce: io_bytes=$ib, \
expected 8192 (bytes lost or split across records)"
	[ "$off" = "0" ] || fail "fence-coalesce: io_offset=$off, \
expected 0 (window's first IO offset)"
	[ "$FAILS" -eq "$f0" ] && pass fence-coalesce
}

step_fence_disabled() {
	require_probe fence-disabled || return
	mnt="$(ds_mnt "$DS1")"
	f="$mnt/n.bin"
	i=0
	while [ "$i" -lt 3 ]; do
		"${SUDO[@]}" dd if=/dev/zero of="$f" bs=4096 count=1 \
		    oflag=append conv=notrunc status=none ||
			{ fail "fence-disabled: write $i failed"; return; }
		i=$((i + 1))
	done
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	if ! poll_op "$DS1" "$obj" "$OP_WRITE" 3; then
		n="$(count_op "$OP_WRITE")"
		fail "fence-disabled: expected 3 WRITE records at \
window=0, found $n within 30s"
		return
	fi
	n="$(count_op "$OP_WRITE")"
	[ "$n" -eq 3 ] ||
		fail "fence-disabled: expected exactly 3 WRITE records, \
got $n"
	nb="$(grep -c "^REC op=$OP_WRITE .*io_bytes=4096 " "$RECS" ||
		true)"
	[ "$nb" -eq 3 ] ||
		fail "fence-disabled: expected 3 records with \
io_bytes=4096, got $nb"
	[ "$FAILS" -eq "$f0" ] && pass fence-disabled
}

step_flush_order() {
	require_probe flush-order || return
	# DS3's window is 2000ms. The write is still pending when rename
	# runs, so the pre-rename flush is what emits WRITE. On a
	# window=0 dataset the write is already its own record and this
	# step would pass with the flush sites removed.
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/o.bin"
	"${SUDO[@]}" dd if=/dev/zero of="$f" bs=4096 count=1 \
	    status=none || { fail "flush-order: write failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	"${SUDO[@]}" mv "$f" "$f.renamed" ||
		{ fail "flush-order: rename failed"; return; }
	f0="$FAILS"
	if ! poll_op "$DS3" "$obj" "$OP_RENAME" 1; then
		fail "flush-order: no RENAME record within 30s"
		return
	fi
	widx="$(grep -n "^REC op=$OP_WRITE " "$RECS" | head -n 1 |
		cut -d: -f1)"
	ridx="$(grep -n "^REC op=$OP_RENAME " "$RECS" | head -n 1 |
		cut -d: -f1)"
	if [ -z "$widx" ]; then
		fail "flush-order: no WRITE record for the renamed \
object"
	elif [ "$widx" -lt "$ridx" ]; then
		:
	else
		fail "flush-order: WRITE at line $widx not before \
RENAME at line $ridx"
	fi
	[ "$FAILS" -eq "$f0" ] && pass flush-order
}

step_close_flush() {
	require_probe close-flush || return
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/cl.bin"
	"${SUDO[@]}" python3 - "$f" "$WD/close-ready" "$WD/close-done" <<'PY' &
import os
import sys
import time

path, ready, done = sys.argv[1], sys.argv[2], sys.argv[3]
fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644)
hold = os.open(path, os.O_RDONLY)
os.write(fd, b"x" * 4096)
os.close(fd)
open(ready, "w").close()
while not os.path.exists(done):
    time.sleep(0.2)
os.close(hold)
PY
	py=$!
	i=0
	while [ ! -f "$WD/close-ready" ] && [ "$i" -lt 40 ]; do
		sleep 0.25
		i=$((i + 1))
	done
	if [ ! -f "$WD/close-ready" ]; then
		"${SUDO[@]}" touch "$WD/close-done"
		wait "$py" || true
		fail "close-flush: writer did not signal ready"
		return
	fi
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	# hold fd is still open, so zfs_inactive cannot emit this.
	if ! poll_op "$DS3" "$obj" "$OP_WRITE" 1; then
		fail "close-flush: pending window not flushed by \
close(2) within 30s"
		"${SUDO[@]}" touch "$WD/close-done"
		wait "$py" || true
		return
	fi
	n="$(count_op "$OP_WRITE")"
	[ "$n" -eq 1 ] ||
		fail "close-flush: expected 1 record, got $n"
	line="$(grep "^REC op=$OP_WRITE " "$RECS" | head -n 1)"
	ib="$(field "$line" io_bytes)"
	[ "$ib" = "4096" ] ||
		fail "close-flush: io_bytes=$ib, expected 4096"
	"${SUDO[@]}" touch "$WD/close-done"
	wait "$py" || true
	[ "$FAILS" -eq "$f0" ] && pass close-flush
}

step_byte_completeness() {
	require_probe byte-completeness || return
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/b.bin"
	"${SUDO[@]}" python3 - "$f" <<'PY'
import os
import sys

fd = os.open(sys.argv[1], os.O_CREAT | os.O_WRONLY | os.O_TRUNC,
             0o644)
i = 0
while i < 10:
    os.write(fd, b"y" * 1024)
    i += 1
os.close(fd)
PY
	[ $? -eq 0 ] || {
		fail "byte-completeness: 10x1KB writes failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	if ! poll_op "$DS3" "$obj" "$OP_WRITE" 1; then
		fail "byte-completeness: no WRITE record within 30s"
		return
	fi
	n="$(count_op "$OP_WRITE")"
	[ "$n" -eq 1 ] || fail "byte-completeness: expected 1 record \
for 10 writes inside one window, got $n"
	line="$(grep "^REC op=$OP_WRITE " "$RECS" | head -n 1)"
	ib="$(field "$line" io_bytes)"
	off="$(field "$line" io_offset)"
	[ "$ib" = "10240" ] || fail "byte-completeness: io_bytes=$ib, \
expected 10240"
	[ "$off" = "0" ] || fail "byte-completeness: io_offset=$off, \
expected 0"
	[ "$FAILS" -eq "$f0" ] && pass byte-completeness
}

step_preflight
step_write_visible
step_read_visible
step_gate_off
step_zero_byte
step_fence_coalesce
step_fence_disabled
step_flush_order
step_close_flush
step_byte_completeness

if [ "$FAILS" -gt 0 ]; then
	printf 'IO-E2E: %d FAILURE(S)\n' "$FAILS"
	exit 1
fi
printf 'IO-E2E: ALL PASS\n'
exit 0
