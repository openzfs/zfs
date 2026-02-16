#!/bin/bash
# shellcheck disable=SC2329
# SPDX-License-Identifier: CDDL-1.0
#
# Manual test runner for zoned_uid tests
# Run this script as root to test against the installed ZFS system
#
# Usage: sudo ./run_manual_tests.sh [pool_name] [test_numbers...]
#
# Examples:
#   sudo ./run_manual_tests.sh                    # Run all tests on default pool
#   sudo ./run_manual_tests.sh mypool             # Run all tests on mypool
#   sudo ./run_manual_tests.sh mypool 6 7 8      # Run tests 006, 007, 008 only
#

set -e

# Configuration
TESTPOOL="${1:-sharec-zpool}"
shift 2>/dev/null || true

# Test UIDs (must match zoned_uid.cfg)
ZONED_TEST_UID=100000
ZONED_OTHER_UID=200000

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results
PASSED=0
FAILED=0
SKIPPED=0

# Helper functions
log_note() {
    echo -e "${YELLOW}NOTE:${NC} $*"
}

log_pass() {
    echo -e "${GREEN}PASS:${NC} $*"
    ((PASSED++))
}

log_fail() {
    echo -e "${RED}FAIL:${NC} $*"
    ((FAILED++))
}

log_skip() {
    echo -e "${YELLOW}SKIP:${NC} $*"
    ((SKIPPED++))
}

cleanup_test_dataset() {
    zfs destroy -rf "${TESTPOOL}/zoned_uid_test" 2>/dev/null || true
    zfs destroy -rf "${TESTPOOL}/outside" 2>/dev/null || true
}

# Check prerequisites
check_prereqs() {
    if [[ $EUID -ne 0 ]]; then
        echo "This script must be run as root"
        exit 1
    fi

    if ! zpool list "$TESTPOOL" &>/dev/null; then
        echo "Pool '$TESTPOOL' does not exist"
        echo "Usage: $0 [pool_name] [test_numbers...]"
        exit 1
    fi

    if ! zfs get zoned_uid "$TESTPOOL" &>/dev/null; then
        echo "zoned_uid property not supported by this ZFS version"
        exit 1
    fi

    # Check if test UIDs exist (or can be used with sudo -u)
    if ! id "$ZONED_TEST_UID" &>/dev/null; then
        log_note "UID $ZONED_TEST_UID doesn't exist as a user, tests will create it temporarily"
    fi

    if ! command -v unshare &>/dev/null; then
        echo "unshare command not found (required for user namespace tests)"
        exit 1
    fi

    echo "Prerequisites OK. Testing on pool: $TESTPOOL"
    echo "Test UID: $ZONED_TEST_UID, Other UID: $ZONED_OTHER_UID"
    echo ""
}

# Ensure test users exist
ensure_test_users() {
    # Create test users if they don't exist
    if ! id "$ZONED_TEST_UID" &>/dev/null; then
        useradd -u "$ZONED_TEST_UID" -M -N -s /bin/false "zfs_test_$ZONED_TEST_UID" 2>/dev/null || true
    fi
    if ! id "$ZONED_OTHER_UID" &>/dev/null; then
        useradd -u "$ZONED_OTHER_UID" -M -N -s /bin/false "zfs_test_$ZONED_OTHER_UID" 2>/dev/null || true
    fi
}

#
# Test 001: Property set/get/clear
#
test_001() {
    echo "=== Test 001: zoned_uid property can be set and retrieved ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"

    # Check default value
    local default_val
    default_val=$(zfs get -H -o value zoned_uid "${TESTPOOL}/zoned_uid_test")
    if [[ "$default_val" != "0" ]]; then
        log_fail "Default zoned_uid should be 0, got: $default_val"
        return
    fi

    # Set value
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    local set_val
    set_val=$(zfs get -H -o value zoned_uid "${TESTPOOL}/zoned_uid_test")
    if [[ "$set_val" != "$ZONED_TEST_UID" ]]; then
        log_fail "zoned_uid should be $ZONED_TEST_UID, got: $set_val"
        return
    fi

    # Clear value
    zfs set zoned_uid=0 "${TESTPOOL}/zoned_uid_test"
    local cleared_val
    cleared_val=$(zfs get -H -o value zoned_uid "${TESTPOOL}/zoned_uid_test")
    if [[ "$cleared_val" != "0" ]]; then
        log_fail "Cleared zoned_uid should be 0, got: $cleared_val"
        return
    fi

    cleanup_test_dataset
    log_pass "zoned_uid property can be set and retrieved"
}

#
# Test 006: Create child dataset from user namespace
#
test_006() {
    echo "=== Test 006: Authorized user namespace can create child datasets ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"

    log_note "Attempting to create child dataset from user namespace..."
    local create_result
    create_result=$(sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs create "${TESTPOOL}/zoned_uid_test/child" 2>&1) || true

    if ! zfs list "${TESTPOOL}/zoned_uid_test/child" &>/dev/null; then
        log_fail "Failed to create child dataset from user namespace: $create_result"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Authorized user namespace can create child datasets"
}

#
# Test 007: Create snapshot from user namespace
#
test_007() {
    echo "=== Test 007: Authorized user namespace can create snapshots ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    zfs create "${TESTPOOL}/zoned_uid_test/child"

    log_note "Attempting to create snapshot from user namespace..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs snapshot "${TESTPOOL}/zoned_uid_test/child@snap1" 2>&1 || true

    if ! zfs list -t snapshot "${TESTPOOL}/zoned_uid_test/child@snap1" &>/dev/null; then
        log_fail "Failed to create snapshot from user namespace"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Authorized user namespace can create snapshots"
}

#
# Test 008: Destroy from user namespace (child OK, root protected)
#
test_008() {
    echo "=== Test 008: Authorized user namespace can destroy children but not delegation root ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    zfs create "${TESTPOOL}/zoned_uid_test/child1"
    zfs create "${TESTPOOL}/zoned_uid_test/child2"
    zfs snapshot "${TESTPOOL}/zoned_uid_test/child1@snap1"

    # Destroy snapshot (should succeed)
    log_note "Destroying snapshot from user namespace..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs destroy "${TESTPOOL}/zoned_uid_test/child1@snap1" 2>&1 || true

    if zfs list -t snapshot "${TESTPOOL}/zoned_uid_test/child1@snap1" &>/dev/null; then
        log_fail "Snapshot should have been destroyed"
        cleanup_test_dataset
        return
    fi

    # Destroy child (should succeed)
    log_note "Destroying child dataset from user namespace..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs destroy "${TESTPOOL}/zoned_uid_test/child1" 2>&1 || true

    if zfs list "${TESTPOOL}/zoned_uid_test/child1" &>/dev/null; then
        log_fail "Child dataset should have been destroyed"
        cleanup_test_dataset
        return
    fi

    # Destroy delegation root (should FAIL)
    log_note "Attempting to destroy delegation root (should fail)..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs destroy "${TESTPOOL}/zoned_uid_test" 2>&1 || true

    if ! zfs list "${TESTPOOL}/zoned_uid_test" &>/dev/null; then
        log_fail "Delegation root should be protected from destruction"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Authorized user namespace can destroy children but not delegation root"
}

#
# Test 009: Rename within subtree only
#
test_009() {
    echo "=== Test 009: Authorized user namespace can rename within delegation subtree only ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    zfs create "${TESTPOOL}/zoned_uid_test/child1"
    zfs create "${TESTPOOL}/outside"

    # Rename within subtree (should succeed)
    log_note "Renaming within delegation subtree..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs rename "${TESTPOOL}/zoned_uid_test/child1" \
        "${TESTPOOL}/zoned_uid_test/child1_renamed" 2>&1 || true

    if ! zfs list "${TESTPOOL}/zoned_uid_test/child1_renamed" &>/dev/null; then
        log_fail "Rename within subtree should have succeeded"
        cleanup_test_dataset
        return
    fi

    # Rename outside subtree (should FAIL)
    log_note "Attempting to rename outside subtree (should fail)..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs rename "${TESTPOOL}/zoned_uid_test/child1_renamed" \
        "${TESTPOOL}/outside/escaped" 2>&1 || true

    if zfs list "${TESTPOOL}/outside/escaped" &>/dev/null; then
        log_fail "Rename outside subtree should have been denied"
        cleanup_test_dataset
        return
    fi

    # Verify original still exists
    if ! zfs list "${TESTPOOL}/zoned_uid_test/child1_renamed" &>/dev/null; then
        log_fail "Dataset should remain in delegation subtree"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Authorized user namespace can rename within delegation subtree only"
}

#
# Test 010: Set properties from user namespace
#
test_010() {
    echo "=== Test 010: Authorized user namespace can set properties ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    zfs create "${TESTPOOL}/zoned_uid_test/child"

    # Set quota
    log_note "Setting quota from user namespace..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs set quota=100M "${TESTPOOL}/zoned_uid_test/child" 2>&1 || true

    local quota
    quota=$(zfs get -H -o value quota "${TESTPOOL}/zoned_uid_test/child")
    if [[ "$quota" != "100M" ]]; then
        log_fail "Quota not set correctly: expected 100M, got $quota"
        cleanup_test_dataset
        return
    fi

    # Set compression
    log_note "Setting compression from user namespace..."
    sudo -u "#${ZONED_TEST_UID}" unshare --user --map-root-user \
        zfs set compression=lz4 "${TESTPOOL}/zoned_uid_test/child" 2>&1 || true

    local comp
    comp=$(zfs get -H -o value compression "${TESTPOOL}/zoned_uid_test/child")
    if [[ "$comp" != "lz4" ]]; then
        log_fail "Compression not set correctly: expected lz4, got $comp"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Authorized user namespace can set properties"
}

#
# Test 011: Unauthorized namespace denied
#
test_011() {
    echo "=== Test 011: Unauthorized user namespace cannot perform write operations ==="
    cleanup_test_dataset

    zfs create "${TESTPOOL}/zoned_uid_test"
    zfs set "zoned_uid=$ZONED_TEST_UID" "${TESTPOOL}/zoned_uid_test"
    zfs create "${TESTPOOL}/zoned_uid_test/child"

    log_note "Testing access from WRONG user namespace (UID $ZONED_OTHER_UID)..."

    # Create should fail
    log_note "Attempting create from wrong namespace..."
    sudo -u "#${ZONED_OTHER_UID}" unshare --user --map-root-user \
        zfs create "${TESTPOOL}/zoned_uid_test/unauthorized" 2>&1 || true

    if zfs list "${TESTPOOL}/zoned_uid_test/unauthorized" &>/dev/null; then
        log_fail "Create from unauthorized namespace should have been denied"
        cleanup_test_dataset
        return
    fi

    # Snapshot should fail
    log_note "Attempting snapshot from wrong namespace..."
    sudo -u "#${ZONED_OTHER_UID}" unshare --user --map-root-user \
        zfs snapshot "${TESTPOOL}/zoned_uid_test/child@unauth" 2>&1 || true

    if zfs list -t snapshot "${TESTPOOL}/zoned_uid_test/child@unauth" &>/dev/null; then
        log_fail "Snapshot from unauthorized namespace should have been denied"
        cleanup_test_dataset
        return
    fi

    # Destroy should fail
    log_note "Attempting destroy from wrong namespace..."
    sudo -u "#${ZONED_OTHER_UID}" unshare --user --map-root-user \
        zfs destroy "${TESTPOOL}/zoned_uid_test/child" 2>&1 || true

    if ! zfs list "${TESTPOOL}/zoned_uid_test/child" &>/dev/null; then
        log_fail "Destroy from unauthorized namespace should have been denied"
        cleanup_test_dataset
        return
    fi

    cleanup_test_dataset
    log_pass "Unauthorized user namespace cannot perform write operations"
}

# Main
check_prereqs
ensure_test_users

# Determine which tests to run
tests_to_run=("$@")
if [[ ${#tests_to_run[@]} -eq 0 ]]; then
    tests_to_run=(1 6 7 8 9 10 11)
fi

echo "Running tests: ${tests_to_run[*]}"
echo ""

for test_num in "${tests_to_run[@]}"; do
    # Pad to 3 digits
    padded=$(printf "%03d" "$test_num")
    func_name="test_${padded}"

    if declare -f "$func_name" > /dev/null; then
        $func_name
        echo ""
    else
        log_skip "Test $padded not implemented in manual runner"
    fi
done

# Summary
echo "========================================"
echo "Test Summary"
echo "========================================"
echo -e "${GREEN}Passed:${NC}  $PASSED"
echo -e "${RED}Failed:${NC}  $FAILED"
echo -e "${YELLOW}Skipped:${NC} $SKIPPED"
echo ""

if [[ $FAILED -gt 0 ]]; then
    exit 1
fi
exit 0
