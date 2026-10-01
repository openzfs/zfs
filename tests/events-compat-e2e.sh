#!/usr/bin/env bash
#
# tests/events-compat-e2e.sh - pool compatibility gate for events=on.
#
# libzfs refuses events=on when the pool's compatibility set excludes
# org.openzfs:events. events=off is still allowed. compatibility=legacy
# is the same refusal (the kernel rejects that set on its own; this
# script sees the libzfs error, which is the contract callers hit).
# A pool with compatibility=off must still accept events=on, so the
# gate is not a blanket refusal.
#
# File vdevs under /var/tmp. Does not touch testpool.
# Usage: bash tests/events-compat-e2e.sh
# Exit 0 = every case passed; exit 1 = first failure.
#
# Environment overrides: ZFS, ZPOOL.

set -u

ZFS="${ZFS:-/usr/local/sbin/zfs}"
ZPOOL="${ZPOOL:-/usr/local/sbin/zpool}"
RUNID="$$"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WD="/var/tmp/events-compat-$RUNID"
COMPAT_SRC="$REPO/cmd/zpool/compatibility.d/openzfs-2.4"

SUDO=()
if [ "$(id -u)" -ne 0 ]; then
	SUDO=(sudo)
fi

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

pass() {
	echo "PASS $*"
}

cleanup() {
	for p in "ce-file-$RUNID" "ce-legacy-$RUNID" "ce-off-$RUNID"; do
		"${SUDO[@]}" "$ZPOOL" destroy -f "$p" >/dev/null 2>&1 || true
	done
	rm -rf "$WD"
}
trap cleanup EXIT

[ -f "$COMPAT_SRC" ] || fail "missing $COMPAT_SRC"
mkdir -p "$WD" || fail "mkdir $WD"

# openzfs-2.4 lists no "events" token. Absolute path: openat() ignores
# the compatibility.d dirfd when the name is absolute. Trailing newline
# is required; the loader NULs the last byte of the file.
cp "$COMPAT_SRC" "$WD/no-events"
printf '\n' >> "$WD/no-events"
grep -qx events "$WD/no-events" &&
	fail "compat file contains events; it would not exclude the feature"

make_disk() {
	truncate -s 256M "$1" || fail "truncate $1"
}

# expect_refuse <pool> <dataset>
# events=on must fail naming the feature; events=off must succeed;
# the feature must still be disabled.
expect_refuse() {
	_pool="$1"
	_ds="$2"
	_err="$WD/set.err"
	"${SUDO[@]}" "$ZFS" set events=on "$_ds" >"$WD/set.out" 2>"$_err"
	_rc=$?
	# A crash is not a refusal. zfs(8) returns 255 (-1) for a rejected
	# property. Signal death is 129-192 (139 is SIGSEGV).
	if [ "$_rc" -eq 0 ]; then
		fail "$_pool: events=on succeeded; expected refusal"
	fi
	if [ "$_rc" -ge 129 ] && [ "$_rc" -le 192 ]; then
		fail "$_pool: zfs set crashed (rc=$_rc), not a clean refusal"
	fi
	_msg="$(cat "$_err")"
	case "$_msg" in
	*org.openzfs:events*) ;;
	*) fail "$_pool: refusal does not name org.openzfs:events: $_msg" ;;
	esac
	_val="$("${SUDO[@]}" "$ZFS" get -H -o value events "$_ds")"
	[ "$_val" = "off" ] ||
		fail "$_pool: events=$_val after refused set, expected off"
	"${SUDO[@]}" "$ZFS" set events=off "$_ds" ||
		fail "$_pool: events=off was refused"
	_feat="$("${SUDO[@]}" "$ZPOOL" get -H -o value feature@events "$_pool")"
	[ "$_feat" = "disabled" ] ||
		fail "$_pool: feature@events=$_feat, expected disabled"
	pass "refuse $_pool"
}

make_disk "$WD/file.img"
"${SUDO[@]}" "$ZPOOL" create -f -o compatibility="$WD/no-events" \
	"ce-file-$RUNID" "$WD/file.img" ||
	fail "create compatibility-file pool"
"${SUDO[@]}" "$ZFS" create "ce-file-$RUNID/fs" ||
	fail "create dataset on compatibility-file pool"
expect_refuse "ce-file-$RUNID" "ce-file-$RUNID/fs"

make_disk "$WD/legacy.img"
"${SUDO[@]}" "$ZPOOL" create -f -o compatibility=legacy \
	"ce-legacy-$RUNID" "$WD/legacy.img" ||
	fail "create compatibility=legacy pool"
"${SUDO[@]}" "$ZFS" create "ce-legacy-$RUNID/fs" ||
	fail "create dataset on legacy pool"
expect_refuse "ce-legacy-$RUNID" "ce-legacy-$RUNID/fs"

make_disk "$WD/off.img"
"${SUDO[@]}" "$ZPOOL" create -f -o compatibility=off \
	"ce-off-$RUNID" "$WD/off.img" ||
	fail "create compatibility=off pool"
"${SUDO[@]}" "$ZFS" create "ce-off-$RUNID/fs" ||
	fail "create dataset on unconstrained pool"
"${SUDO[@]}" "$ZFS" set events=on "ce-off-$RUNID/fs" ||
	fail "compatibility=off: events=on was refused"
_val="$("${SUDO[@]}" "$ZFS" get -H -o value events "ce-off-$RUNID/fs")"
[ "$_val" = "on" ] ||
	fail "compatibility=off: events=$_val, expected on"
pass "allow compatibility=off"

echo "E2E: ALL PASS"
exit 0
