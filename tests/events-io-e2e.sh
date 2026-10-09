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
# This suite runs under bash; [ ] tests are intentional style here.
# shellcheck disable=SC2292
# Case statements here are guard-style; default arms are noise.
# shellcheck disable=SC2249
#
# tests/events-io-e2e.sh - end-to-end validation of the IO event
# feature against a live system running the leaf-01 kernel.
#
# All record assertions go through a compiled wire probe (the
# lzc_get_events ioctl), never through `zfs events` text output or
# the zmetad database: this is wire-level behavior testing.  Records
# land in the ring after txg sync (~5s default), so every presence
# check polls up to ~30s.  Absence checks first poll until a control
# record (CREATE) for the same object appears - proof the ring is
# draining past the operations under test - and only then assert
# absence over a bounded poll window.
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
#   fence-coalesce     window=5000: two writes 1s apart, close ->
#                      ONE merged WRITE record, io_bytes=8192
#   fence-disabled     window=0: 3 writes -> exactly 3 records
#   flush-order        window=5000: a still-pending WRITE is flushed
#                      before the RENAME, so WRITE precedes RENAME
#   close-flush        close(2) flushes a young window while another
#                      fd holds the inode, so inactive cannot be the
#                      emitter
#   byte-completeness  10 x 1KB writes in one window -> one record,
#                      io_bytes=10240
#   cleanup            EXIT trap destroys datasets + workdir
#
# Every step that asserts exact record counts also asserts the
# probe's META lost= counter is 0: a wrapped ring invalidates the
# counts, so the failure is reported as ring loss, not as a count
# mismatch.
#
# Usage: bash tests/events-io-e2e.sh  (root or passwordless sudo)
# Exit 0 = every assertion passed; nonzero = one or more failed
# (all assertions run; failures are collected, not fatal).
#
# Environment overrides: ZFS, ZPOOL, ZIO_E2E_KEEP (keep workdir).
#
# Parallel-run safety: every dataset this suite creates carries the
# per-run ID ($$) and the stale-dataset sweep in preflight matches
# ONLY that run ID, so concurrent runs can never destroy each
# other's datasets.

set -u

ZFS="${ZFS:-/usr/local/sbin/zfs}"
ZPOOL="${ZPOOL:-/usr/local/sbin/zpool}"
POOL="testpool"
BASE_DS="$POOL/fs1"
RUNID="$$"
DS1="$BASE_DS/io-e2e-$RUNID"
DS2="$BASE_DS/io-e2e-off-$RUNID"
DS3="$BASE_DS/io-e2e-fence-$RUNID"
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

# Writer-identity execution (fixes the empty-SUDO expansion bug: as
# root, "${SUDO[@]}" -u nobody dd became "-u nobody dd", rc 127).
# As root, drop privileges with runuser, else setpriv, else su.
# Non-root keeps sudo -u.  Set up once by setup_runas.
WRITER=""
RUNAS=()
RUNAS_MODE=""

setup_runas() {
	# $1 = writer username
	WRITER="$1"
	if [ "$(id -u)" -eq 0 ]; then
		if command -v runuser >/dev/null 2>&1; then
			RUNAS=(runuser -u "$WRITER" --)
			RUNAS_MODE="argv"
		elif command -v setpriv >/dev/null 2>&1; then
			RUNAS=(setpriv --reuid="$(id -u "$WRITER")" \
			    --regid="$(id -g "$WRITER")" --clear-groups)
			RUNAS_MODE="argv"
		else
			RUNAS=()
			RUNAS_MODE="su"
		fi
	else
		RUNAS=(sudo -u "$WRITER")
		RUNAS_MODE="argv"
	fi
}

# run_as_writer <cmd> [args...]: run a command with WRITER's identity.
run_as_writer() {
	if [ "$RUNAS_MODE" = "su" ]; then
		su "$WRITER" -s /bin/sh -c "$(printf '%q ' "$@")"
	else
		"${RUNAS[@]}" "$@"
	fi
}

CREATED1=0
CREATED2=0
CREATED3=0
WD=""
PROBE_OK=0
RECS=""
FAILS=0
STEP="init"

pass() { printf 'PASS: %s\n' "$1"; }
notice() { printf 'NOTICE: %s\n' "$1"; }
# fail() writes to stderr so its diagnostics survive command
# substitution in callers (stdout stays machine-value-only).
fail() {
	FAILS=$((FAILS + 1))
	printf 'FAIL: %s\n' "$1" >&2
}

cleanup() {
	status=$?
	ok=1
	if [ "$CREATED1" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS1" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS1" >&2; ok=0; }
	fi
	if [ "$CREATED2" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS2" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS2" >&2; ok=0; }
	fi
	if [ "$CREATED3" -eq 1 ]; then
		"${SUDO[@]}" "$ZFS" destroy -R "$DS3" >/dev/null 2>&1 ||
			{ printf 'FAIL: cleanup: destroy %s\n' "$DS3" >&2; ok=0; }
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
# TERM/INT must clean up too (E2E-9): an EXIT-only trap leaks the
# datasets and workdir when the suite is killed between steps.
# Routing the signal through exit runs the EXIT trap above.
trap 'exit 143' TERM
trap 'exit 130' INT

ds_mnt() {
	"$ZFS" get -H -o value mountpoint "$1"
}

# find_writer: pick an unprivileged user for identity tests and build
# the RUNAS command prefix for it.
find_writer() {
	# $1 = step label
	local u
	for u in nobody daemon; do
		if id -u "$u" >/dev/null 2>&1; then
			setup_runas "$u"
			return 0
		fi
	done
	fail "$1: no unprivileged user found (nobody/daemon)"
	return 1
}

# run_probe <dataset> <object-id>: dump that object's records (ring
# order) to $RECS.  Empty object id 0 means "all objects".  A nonzero
# probe exit is a hard step failure (never a silent empty $RECS).
run_probe() {
	local rc
	RECS="$WD/recs.txt"
	"${SUDO[@]}" env \
	    LD_LIBRARY_PATH="$REPO/lib/libzfs_core/.libs:$REPO/lib/libnvpair/.libs" \
	    "$WD/probe" "$1" "${2:-0}" >"$RECS" 2>"$WD/probe.err"
	rc=$?
	if [ "$rc" -ne 0 ]; then
		fail "$STEP: wire probe exited $rc: $(head -c 200 "$WD/probe.err" 2>/dev/null)"
		return "$rc"
	fi
	return 0
}

# count_op <op-code>: number of records with that op in $RECS.
count_op() {
	grep -c "^REC op=$1 " "$RECS" || true
}

# meta_lost: the probe META line's lost= value (empty when absent).
meta_lost() {
	sed -n 's/^META .*lost=\([0-9]*\).*$/\1/p' "$RECS" | head -n 1
}

# assert_no_loss: steps with exact-count assertions assume the ring
# never wrapped; verify that against the probe's own lost counter.
assert_no_loss() {
	local lost
	lost="$(meta_lost)"
	case "$lost" in
	0) return 0 ;;
	"") fail "$STEP: probe printed no META lost= line"; return 1 ;;
	*) fail "$STEP: probe reports lost=$lost (ring wrapped; count assertions invalid)"
	   return 1 ;;
	esac
}

# poll_op <dataset> <object> <op> <n>: poll up to ~30s (txg sync is
# ~5s) until at least <n> records with <op> exist for <object>.
# rc: 0 found, 1 timeout, 2 probe failure (already reported).
poll_op() {
	local i c
	i=0
	while [ "$i" -lt 15 ]; do
		run_probe "$1" "$2" || return 2
		c="$(count_op "$3")"
		[ "$c" -ge "$4" ] && return 0
		sleep 2
		i=$((i + 1))
	done
	return 1
}

# wait_drained <dataset> <object>: poll until the control CREATE
# record for <object> appears.  The CREATE is logged by the same
# operation sequence under test, so its arrival proves the ring is
# draining PAST those operations - only then is an absence assertion
# (no WRITE/READ) meaningful instead of vacuous.
# rc: 0 drained, 1 timeout, 2 probe failure (already reported).
wait_drained() {
	poll_op "$1" "$2" "$OP_CREATE" 1
}

# assert_absent <dataset> <object> <op>: bounded poll (~10s) that
# <op> never shows up for <object>.  Called only after wait_drained.
assert_absent() {
	local i
	i=0
	while [ "$i" -lt 5 ]; do
		run_probe "$1" "$2" || return 2
		[ "$(count_op "$3")" -eq 0 ] || return 1
		sleep 2
		i=$((i + 1))
	done
	return 0
}

# field <line> <key>: value of key= in a REC line.  Extraction runs
# to the next " word=" token boundary (or end of line), so values
# containing spaces - e.g. name=my file - are returned intact; the
# probe prints name= as the LAST field of every REC line.
field() {
	printf '%s\n' "$1" | awk -v key="$2" '
	{
		if (match($0, "(^| )" key "=")) {
			v = substr($0, RSTART + RLENGTH)
			if (match(v, " [A-Za-z_]+="))
				v = substr(v, 1, RSTART - 1)
			print v
		}
	}'
}

step_preflight() {
	STEP=preflight
	"$ZPOOL" list -H -o name "$POOL" >/dev/null 2>&1 || {
		fail "preflight: pool $POOL not imported"
		return
	}

	# Stale-run sweep: destroy ONLY datasets carrying this run's
	# ID (leftovers from an earlier attempt of this same run,
	# e.g. after a signal between create and the EXIT trap).
	# Wildcard sweeps across other runs' IDs are deliberately
	# NOT done: parallel suites must not destroy each other.
	while read -r d; do
		case "$d" in
		*-io-e2e-"$RUNID"|*-io-e2e-off-"$RUNID"|*-io-e2e-fence-"$RUNID")
			"${SUDO[@]}" "$ZFS" destroy -R "$d" \
			    >/dev/null 2>&1 || true
			;;
		*) ;;
		esac
	done < <("$ZFS" list -r -H -o name "$BASE_DS" 2>/dev/null)

	# Same leak class as the schema suite's preflight (E2E-9):
	# destroy e2e-* / io-e2e-* children of $BASE_DS older than this
	# run's pid scope.  Names minted by a live run always carry a
	# pid >= this run's pid, so nothing in use is touched; the
	# sweep only reaps leftovers of runs killed before their traps
	# could fire.
	while read -r d; do
		case "$d" in
		"$BASE_DS"/e2e-*|"$BASE_DS"/io-e2e-*) ;;
		*) continue ;;
		esac
		case "$d" in
		*-"$RUNID") continue ;;  # this run's own names: handled above
		*) ;;
		esac
		_rid="${d##*-}"
		case "$_rid" in
		''|*[!0-9]*) continue ;;
		*) continue ;;
		esac
		# Never reap a name minted by a LIVE run, even when its
		# pid sorts below ours: a live run's dataset is in use.
		# The schema suite mints a systemd unit per run
		# (zmd-e2e-<pid>); this suite mints runs with no unit, so
		# fall back to "is the pid still alive".
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

	# events_io rides on the general event log: enable events on the
	# base dataset first (children inherit it), exactly as the schema
	# suite does, then enable events_io on the children.
	"${SUDO[@]}" "$ZFS" set events=on "$BASE_DS" ||
		{ fail "preflight: set events=on $BASE_DS"; return; }
	# Normalise events_io on the base BEFORE the dependency case
	# below: events_io is inherited, so a leftover events_io=on on
	# the base dataset (a previous session's manual testing) makes
	# the events=off on DS2 fail with the dependency refusal and
	# aborts the whole suite.  The suite must not depend on the
	# pool's pre-existing property state.
	"${SUDO[@]}" "$ZFS" set events_io=off "$BASE_DS" ||
		{ fail "preflight: reset events_io $BASE_DS"; return; }

	# Negative case (E2E-3): with events=off on a child, enabling
	# events_io must be REFUSED.  `zfs set` reports only the generic
	# ioctl error class for a rejected property (ENOTSUP maps to
	# "pool and or dataset must be upgraded..."), so the dependency
	# is asserted against the kernel's own diagnostic - the cmn_err
	# "... the events property must be enabled first" - which lands
	# in the kernel log.  Exercised on DS2, restored immediately after.
	"${SUDO[@]}" "$ZFS" set events=off "$DS2" ||
		{ fail "preflight: set events=off $DS2"; return; }
	klog_before="$("${SUDO[@]}" dmesg 2>/dev/null | wc -l | tr -d ' ')"
	[ -n "$klog_before" ] || klog_before=0
	refuse_out="$("${SUDO[@]}" "$ZFS" set events_io=on "$DS2" 2>&1)"
	refuse_rc=$?
	if [ "$refuse_rc" -eq 0 ]; then
		fail "preflight: events_io=on accepted with events=off on $DS2; expected refusal: $refuse_out"
	fi
	_io_val="$("${SUDO[@]}" "$ZFS" get -H -o value events_io "$DS2")"
	[ "$_io_val" = "off" ] ||
		fail "preflight: events_io=$_io_val after refused set (expected off)"
	# Require the kernel's naming of `events` whenever dmesg is
	# readable (a nonzero pre-attempt line count): a dmesg that is
	# restricted or absent leaves klog_before=0 and skips only this
	# string check, never the refusal/state assertions above.
	if [ "$klog_before" -gt 0 ]; then
		_klog_new="$("${SUDO[@]}" dmesg 2>/dev/null | \
		    tail -n "+$((klog_before + 1))")"
		printf '%s\n' "$_klog_new" | grep -q \
		    'events property must be enabled first' ||
			fail "preflight: kernel refused events_io but did not name the events dependency"
	fi
	"${SUDO[@]}" "$ZFS" set events=on "$DS2" ||
		{ fail "preflight: restore events=on $DS2"; return; }

	"${SUDO[@]}" "$ZFS" set events_io=on events_io_window=0 "$DS1" ||
		{ fail "preflight: props $DS1"; return; }
	"${SUDO[@]}" "$ZFS" set events_io=off "$DS2" ||
		{ fail "preflight: props $DS2"; return; }
	# Wide fence margin: window=5000 with writes 1s apart keeps
	# the coalesce test clear of the fence boundary even on a
	# loaded host (2000ms was race-prone).
	"${SUDO[@]}" "$ZFS" set events_io=on events_io_window=5000 "$DS3" ||
		{ fail "preflight: props $DS3"; return; }
	w="$(ds_mnt "$DS1")"
	case "$w" in
	/*) ;;
	*) fail "preflight: unexpected mountpoint '$w'"; return ;;
	esac

	WD="$(mktemp -d /var/tmp/zio-e2e.XXXXXX)"

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
	STEP=write-visible
	require_probe write-visible || return
	mnt="$(ds_mnt "$DS1")"
	"${SUDO[@]}" chmod 0777 "$mnt"
	find_writer write-visible || return
	uid="$(id -u "$WRITER")"
	gid="$(id -g "$WRITER")"
	f="$mnt/w.bin"
	run_as_writer dd if=/dev/zero of="$f" bs=4096 \
	    count=1 status=none || {
		fail "write-visible: dd as $WRITER failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	poll_op "$DS1" "$obj" "$OP_WRITE" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "write-visible: no WRITE record within 30s"
		return
	fi
	assert_no_loss
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
	STEP=read-visible
	require_probe read-visible || return
	mnt="$(ds_mnt "$DS1")"
	f="$mnt/w.bin"
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	find_writer read-visible || return
	f0="$FAILS"
	run_as_writer dd if="$f" of=/dev/null bs=4096 \
	    count=1 status=none || {
		fail "read-visible: dd read failed"
		return
	}
	poll_op "$DS1" "$obj" "$OP_READ" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "read-visible: no READ record within 30s"
		return
	fi
	assert_no_loss
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
	STEP=gate-off
	require_probe gate-off || return
	mnt="$(ds_mnt "$DS2")"
	f="$mnt/g.bin"
	"${SUDO[@]}" dd if=/dev/zero of="$f" bs=4096 count=1 \
	    status=none || { fail "gate-off: write failed"; return; }
	"${SUDO[@]}" dd if="$f" of=/dev/null bs=4096 count=1 \
	    status=none || { fail "gate-off: read failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	# The dd above created g.bin, so a CREATE record must appear
	# even with events_io=off.  Waiting for it proves the ring is
	# draining; the WRITE/READ absence below is then non-vacuous.
	wait_drained "$DS2" "$obj"
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "gate-off: control CREATE record never appeared \
within 30s; ring is not capturing this dataset, IO absence below \
would be vacuous"
		return
	fi
	for op in "$OP_WRITE" "$OP_READ"; do
		assert_absent "$DS2" "$obj" "$op"
		rc=$?
		[ "$rc" -eq 2 ] && return
		[ "$rc" -eq 0 ] ||
			fail "gate-off: op=$op records present with \
events_io=off"
	done
	[ "$FAILS" -eq "$f0" ] && pass gate-off
}

step_zero_byte() {
	STEP=zero-byte
	require_probe zero-byte || return
	mnt="$(ds_mnt "$DS1")"
	f="$mnt/z.bin"
	"${SUDO[@]}" sh -c ": > '$f'" ||
		{ fail "zero-byte: truncate-create failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	# EOF read: 0 bytes transferred must not emit a READ record.
	"${SUDO[@]}" dd if="$f" of=/dev/null bs=4096 count=1 \
	    status=none || { fail "zero-byte: EOF read failed"; return; }
	f0="$FAILS"
	# Control: the truncate-create logged a CREATE for this
	# object; wait for it before asserting WRITE/READ absence.
	wait_drained "$DS1" "$obj"
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "zero-byte: control CREATE record never appeared \
within 30s; absence checks below would be vacuous"
		return
	fi
	for op in "$OP_WRITE" "$OP_READ"; do
		assert_absent "$DS1" "$obj" "$op"
		rc=$?
		[ "$rc" -eq 2 ] && return
		if [ "$rc" -ne 0 ]; then
			if [ "$op" = "$OP_WRITE" ]; then
				fail "zero-byte: WRITE records for a \
zero-byte file"
			else
				fail "zero-byte: READ records for an \
EOF-only read"
			fi
		fi
	done
	[ "$FAILS" -eq "$f0" ] && pass zero-byte
}

step_fence_coalesce() {
	STEP=fence-coalesce
	require_probe fence-coalesce || return
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/c.bin"
	# window=5000ms, writes 1s apart: a 4s margin on each side of
	# the second write, wide enough for a loaded host.
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
	pyrc=$?
	[ "$pyrc" -eq 0 ] || {
		fail "fence-coalesce: timed writes failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	poll_op "$DS3" "$obj" "$OP_WRITE" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "fence-coalesce: no WRITE record within 30s"
		return
	fi
	assert_no_loss
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
	STEP=fence-disabled
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
	poll_op "$DS1" "$obj" "$OP_WRITE" 3
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		n="$(count_op "$OP_WRITE")"
		fail "fence-disabled: expected 3 WRITE records at \
window=0, found $n within 30s"
		return
	fi
	assert_no_loss
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
	STEP=flush-order
	require_probe flush-order || return
	# DS3's window is 5000ms (set by preflight). The write is still
	# pending when rename runs, so the pre-rename flush is what
	# emits WRITE. On a window=0 dataset the write is already its
	# own record and this step would pass with the flush sites
	# removed.
	mnt="$(ds_mnt "$DS3")"
	f="$mnt/o.bin"
	"${SUDO[@]}" dd if=/dev/zero of="$f" bs=4096 count=1 \
	    status=none || { fail "flush-order: write failed"; return; }
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	"${SUDO[@]}" mv "$f" "$f.renamed" ||
		{ fail "flush-order: rename failed"; return; }
	f0="$FAILS"
	poll_op "$DS3" "$obj" "$OP_RENAME" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "flush-order: no RENAME record within 30s"
		return
	fi
	assert_no_loss
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
	STEP=close-flush
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
	poll_op "$DS3" "$obj" "$OP_WRITE" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "close-flush: pending window not flushed by \
close(2) within 30s"
		"${SUDO[@]}" touch "$WD/close-done"
		wait "$py" || true
		return
	fi
	assert_no_loss
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
	STEP=byte-completeness
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
	pyrc=$?
	[ "$pyrc" -eq 0 ] || {
		fail "byte-completeness: 10x1KB writes failed"
		return
	}
	obj="$("${SUDO[@]}" stat -c %i "$f")"
	f0="$FAILS"
	poll_op "$DS3" "$obj" "$OP_WRITE" 1
	rc=$?
	[ "$rc" -eq 2 ] && return
	if [ "$rc" -ne 0 ]; then
		fail "byte-completeness: no WRITE record within 30s"
		return
	fi
	assert_no_loss
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
