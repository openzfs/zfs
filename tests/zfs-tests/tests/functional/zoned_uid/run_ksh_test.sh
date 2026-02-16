#!/bin/bash
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2026 Colin. All rights reserved.
#

#
# Wrapper to run individual zoned_uid ksh tests against the installed ZFS system
#
# Usage: sudo ./run_ksh_test.sh <test_name> [pool_name]
#
# Examples:
#   sudo ./run_ksh_test.sh zoned_uid_006_pos
#   sudo ./run_ksh_test.sh zoned_uid_007_pos sharec-zpool
#   sudo ./run_ksh_test.sh all                 # Run all zoned_uid tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZFS_SRC="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

# Determine test(s) to run
TEST_NAME="${1:-all}"
TESTPOOL="${2:-sharec-zpool}"

# Check prerequisites
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root (sudo)"
    exit 1
fi

if ! zpool list "$TESTPOOL" &>/dev/null; then
    echo "Pool '$TESTPOOL' does not exist"
    echo "Usage: $0 <test_name> [pool_name]"
    exit 1
fi

# Set up ZFS test framework environment
export STF_SUITE="$ZFS_SRC/tests/zfs-tests"
export STF_TOOLS="$ZFS_SRC/tests/test-runner"
export STF_PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH="$STF_PATH:$PATH"

# Test pool configuration - use existing pool with a test filesystem
export TESTPOOL="$TESTPOOL"
export TESTFS="zoned_uid_testfs_$$"
export DISKS=""  # Not using disks directly

# Ensure we have ksh
if ! command -v ksh &>/dev/null; then
    echo "ksh not found. Installing..."
    if command -v pacman &>/dev/null; then
        pacman -S --noconfirm ksh || echo "Please install ksh manually"
    elif command -v apt-get &>/dev/null; then
        apt-get install -y ksh
    fi
fi

# Create logapi.shlib if missing (minimal implementation for standalone runs)
mkdir -p "$STF_TOOLS/include"
if [[ ! -f "$STF_TOOLS/include/logapi.shlib" ]]; then
    cat > "$STF_TOOLS/include/logapi.shlib" << 'LOGAPI'
# Minimal logapi implementation for standalone test runs

_LOG_PASS=0
_LOG_FAIL=0

function log_must
{
    "$@" || {
        echo "ERROR: Command failed: $*"
        exit 1
    }
}

function log_mustnot
{
    "$@" && {
        echo "ERROR: Command should have failed: $*"
        exit 1
    }
    return 0
}

function log_note
{
    echo "NOTE: $*"
}

function log_pass
{
    echo "PASS: $*"
    ((_LOG_PASS++))
}

function log_fail
{
    echo "FAIL: $*"
    ((_LOG_FAIL++))
    exit 1
}

function log_assert
{
    echo ""
    echo "ASSERTION: $*"
    echo "============================================"
}

function log_onexit
{
    trap "$1" EXIT
}

function log_unsupported
{
    echo "UNSUPPORTED: $*"
    exit 4  # STF unsupported exit code
}
LOGAPI
fi

# Create tunables.cfg if missing
if [[ ! -f "$STF_SUITE/include/tunables.cfg" ]]; then
    touch "$STF_SUITE/include/tunables.cfg"
fi

# Create minimal math.shlib if needed
if [[ ! -f "$STF_SUITE/include/math.shlib" ]] || ! grep -q "function floor" "$STF_SUITE/include/math.shlib" 2>/dev/null; then
    # File exists from build, should be fine
    :
fi

# Add helper functions to libtest.shlib supplement
export LIBTEST_SUPPLEMENT="$STF_TOOLS/include/libtest_supplement.shlib"
cat > "$LIBTEST_SUPPLEMENT" << 'SUPPLEMENT'
# Supplement for standalone test runs

function verify_runnable
{
    # For standalone runs, assume global zone
    [[ "$1" == "global" ]] && return 0
    [[ "$1" == "both" ]] && return 0
    return 1
}

function default_setup
{
    # Create test filesystem on existing pool
    log_must zfs create "$TESTPOOL/$TESTFS"
}

function default_cleanup
{
    zfs destroy -rf "$TESTPOOL/$TESTFS" 2>/dev/null || true
}

function is_linux
{
    [[ "$(uname)" == "Linux" ]]
}

function datasetexists
{
    zfs list -H "$1" &>/dev/null
}

function poolexists
{
    zpool list -H "$1" &>/dev/null
}

function get_prop
{
    typeset prop=$1
    typeset dataset=$2
    zfs get -H -o value "$prop" "$dataset"
}

function destroy_pool
{
    # SAFETY: Never destroy the test pool in manual runs!
    echo "WARNING: destroy_pool called but ignored for safety (manual run)"
    return 0
}
SUPPLEMENT

# Function to run a single test
run_single_test() {
    local test_file="$1"
    local test_name
    test_name="$(basename "$test_file" .ksh)"

    echo ""
    echo "========================================"
    echo "Running: $test_name"
    echo "========================================"

    # Create test filesystem
    zfs create "$TESTPOOL/$TESTFS" 2>/dev/null || true

    # Run the test
    local start_time
    start_time=$(date +%s)
    local result=0

    (
        # Source the supplement first
        . "$LIBTEST_SUPPLEMENT"

        # Run the test
        cd "$SCRIPT_DIR"
        ksh "$test_file"
    ) && result=0 || result=$?

    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Cleanup test filesystem
    zfs destroy -rf "$TESTPOOL/$TESTFS" 2>/dev/null || true

    if [[ $result -eq 0 ]]; then
        echo -e "\033[32m[PASS]\033[0m $test_name (${duration}s)"
        return 0
    elif [[ $result -eq 4 ]]; then
        echo -e "\033[33m[SKIP]\033[0m $test_name - unsupported"
        return 0
    else
        echo -e "\033[31m[FAIL]\033[0m $test_name (exit code: $result)"
        return 1
    fi
}

# Main execution
echo "ZFS Test Framework Wrapper"
echo "=========================="
echo "STF_SUITE: $STF_SUITE"
echo "TESTPOOL:  $TESTPOOL"
echo "TESTFS:    $TESTFS"
echo ""

PASSED=0
FAILED=0

if [[ "$TEST_NAME" == "all" ]]; then
    # Run all zoned_uid tests
    for test_file in "$SCRIPT_DIR"/zoned_uid_*_*.ksh; do
        [[ -f "$test_file" ]] || continue
        # shellcheck disable=SC2310
        run_single_test "$test_file" && rc=0 || rc=$?
        if [[ $rc -eq 0 ]]; then
            PASSED=$((PASSED + 1))
        else
            FAILED=$((FAILED + 1))
        fi
    done
else
    # Run specific test
    test_file="$SCRIPT_DIR/${TEST_NAME}.ksh"
    if [[ ! -f "$test_file" ]]; then
        # Try with _pos or _neg suffix
        test_file="$SCRIPT_DIR/${TEST_NAME}_pos.ksh"
        [[ -f "$test_file" ]] || test_file="$SCRIPT_DIR/${TEST_NAME}_neg.ksh"
    fi

    if [[ ! -f "$test_file" ]]; then
        echo "Test not found: $TEST_NAME"
        echo "Available tests:"
        find "$SCRIPT_DIR" -name 'zoned_uid_*_*.ksh' -print0 | \
            xargs -0 -n1 basename | sed 's/.ksh$//'
        exit 1
    fi

    # shellcheck disable=SC2310
    run_single_test "$test_file" && rc=0 || rc=$?
    if [[ $rc -eq 0 ]]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
fi

echo ""
echo "========================================"
echo "Summary: $PASSED passed, $FAILED failed"
echo "========================================"

[[ $FAILED -eq 0 ]] && exit 0 || exit 1
