#!/usr/bin/env bash

######################################################################
# Run the tests of a "failing test -> fix" commit pair and check them
# against the expected outcome (see zfs-precheck.yml).
#
# called on runner:  failfirst-tests.sh fail|pass TITLE TEST...
# called on qemu-vm: failfirst-tests.sh --vm-setup OS
#                    failfirst-tests.sh --vm-test OS TEST
#
# fail: the build is a test commit without its fix.  At least one test
#       must fail, time out, hang or crash the VM.
# pass: the build is the PR head.  Every test must pass without kernel
#       errors (skipped tests only give a warning).
#
# TEST is a path relative to the test suite, as zfs-tests.sh -t takes it
# (tests/functional/<area>/<name>.ksh).  Tests run one at a time on vm1.
# A test that panics or hangs the VM is recorded and the VM is restarted
# for the next test.
######################################################################

set -eu

# Seconds a single test may run before the VM is considered hung.  The
# test runner itself stops a test after 600 seconds; this allows for the
# group's setup and cleanup.  failfirst-detect.py sizes the step timeout
# from it.
PER_TEST_TIMEOUT=${PER_TEST_TIMEOUT:-1500}

# Kernel messages that mean the test hit a bug, even if it passed.
KERR='VERIFY|PANIC|BUG:|Oops|Kernel panic|general protection|Call Trace'

function tdir() {
  case "$1" in
    freebsd*) echo "/usr/local/share/zfs" ;;
    *) echo "/usr/share/zfs" ;;
  esac
}

#############################################
# Inside the qemu vm
#############################################

if [ "${1:-}" == "--vm-setup" ]; then
  # Same environment as qemu-6-tests.sh, without running the suite.
  OS="$2"
  export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
  case "$OS" in
    freebsd*)
      sudo kldstat -n zfs 2>/dev/null && sudo kldunload zfs
      sudo -E ./zfs/scripts/zfs.sh
      sudo newfs -U -t -L tmp /dev/vtbd1 >/dev/null
      sudo mount -o noatime /dev/vtbd1 /var/tmp
      ;;
    *)
      sudo -E modprobe zfs
      sudo mkfs.xfs -fq /dev/vdb
      sudo mount -o noatime /dev/vdb /var/tmp
      f="/sys/module/rcupdate/parameters/rcu_cpu_stall_timeout"
      test -f $f && echo 120 | sudo sh -c "cat > '$f'"
      test -c /dev/watchdog && sudo wdctl --settimeout 120 >/dev/null
      ;;
  esac
  sudo chmod 1777 /var/tmp
  case "$OS" in
    almalinux9|almalinux10|centos-stream*)
      sudo sysctl kernel.io_uring_disabled=0 > /dev/null
      ;;
  esac
  sudo dmesg -c > /dev/null
  exit 0
fi

if [ "${1:-}" == "--vm-test" ]; then
  OS="$2"
  TEST="$3"
  export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
  cd /var/tmp
  rm -rf test_results
  "$(tdir "$OS")"/zfs-tests.sh -vKO -s 3GB -t "$TEST" || true
  exit 0
fi

#############################################
# On the runner
#############################################

EXPECT="$1"
TITLE="$2"
shift 2
TESTS=("$@")

source /var/tmp/env.txt
SCRIPT='$HOME/zfs/.github/workflows/scripts/failfirst-tests.sh'
OUT="$RESPATH/failfirst"
CONSOLE="$RESPATH/vm1/console.txt"
mkdir -p "$OUT"

function alive() {
  timeout 30 ssh -o ConnectTimeout=10 zfs@vm1 true </dev/null &>/dev/null
}

function console_lines() {
  wc -l < "$CONSOLE" 2>/dev/null || echo 0
}

# Restart vm1 after a panic or hang, and reattach its serial console log.
function restart_vm() {
  echo "Restarting vm1"
  sudo virsh destroy vm1 &>/dev/null || true
  sudo virsh start vm1 >/dev/null
  read -r pty <<< "$(sudo virsh ttyconsole vm1)"
  sudo nohup bash -c "cat $pty >> $CONSOLE" &>/dev/null &
  for _ in $(seq 60); do alive && break; sleep 5; done
  ssh zfs@vm1 "$SCRIPT --vm-setup $OS" </dev/null || \
    echo "vm1 did not come back; the remaining tests will have no result"
}

# Run one test on vm1; print its result, one of:
# PASS FAIL KILLED SKIP CRASH HANG KERNEL-ERROR NO-RESULT
function run_test() {
  local t="$1" log="$2" start result kmsg
  start=$(console_lines)

  ssh zfs@vm1 "$SCRIPT --vm-test $OS $t" </dev/null &> "$log" &
  local pid=$! waited=0 crashed=0
  while kill -0 $pid 2>/dev/null; do
    sleep 10
    waited=$((waited+10))
    if sudo tail -n +$((start+1)) "$CONSOLE" 2>/dev/null \
      | grep -qE 'PANIC|Kernel panic|BUG:|Oops'; then
      # Give the kernel a moment to print the rest of the trace.
      sleep 30
      crashed=1
      break
    fi
    if [ $waited -ge "$PER_TEST_TIMEOUT" ]; then
      break
    fi
  done
  if kill -0 $pid 2>/dev/null; then
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
    sudo tail -n +$((start+1)) "$CONSOLE" > "$log.console" 2>/dev/null || true
    if [ $crashed == 1 ]; then
      result="CRASH"
    else
      result="HANG"
    fi
    echo "$result"
    return
  fi
  wait $pid || true

  # Copy the ZTS logs off the VM before anything else can lose them.
  rsync -arL zfs@vm1:/var/tmp/test_results/current/ "${log%.txt}/" \
    &>/dev/null || true

  # Test: /usr/share/zfs/zfs-tests/tests/functional/a/b.ksh (run as root) [00:02] [PASS]
  result=$(grep -F -e "/$t (" -e "/${t%.ksh} (" "$log" 2>/dev/null \
    | grep -oE '\[(PASS|FAIL|KILLED|SKIP|RERAN)\]' | tail -n 1 | tr -d '[]')
  [ "$result" == "RERAN" ] && result="PASS"

  kmsg=$(timeout 60 ssh zfs@vm1 "sudo dmesg -c" </dev/null 2>/dev/null \
    | grep -E "$KERR" | head -n 20 || true)
  if [ -n "$kmsg" ]; then
    echo "$kmsg" > "$log.dmesg"
    case "$result" in
      PASS|SKIP|"") result="KERNEL-ERROR" ;;
    esac
  fi
  echo "${result:-NO-RESULT}"
}

# vm1 runs the tests; vm2 is not used.
ssh zfs@vm1 "$SCRIPT --vm-setup $OS" </dev/null

declare -A RESULT
for t in "${TESTS[@]}"; do
  log="$OUT/$(echo "${t%.ksh}" | tr '/' '_').txt"
  echo "##[group]$t"
  RESULT[$t]=$(run_test "$t" "$log")
  cat "$log" "$log.dmesg" "$log.console" 2>/dev/null | tail -n 200 || true
  echo "##[endgroup]"
  echo "$t: ${RESULT[$t]}"
  case "${RESULT[$t]}" in
    CRASH|HANG) restart_vm ;;
  esac
done

# verdict
failed=0
passed=0
skipped=0
for t in "${TESTS[@]}"; do
  case "${RESULT[$t]}" in
    PASS) passed=$((passed+1)) ;;
    SKIP|NO-RESULT) skipped=$((skipped+1)) ;;
    *) failed=$((failed+1)) ;;
  esac
done

rv=0
if [ "$EXPECT" == "fail" ]; then
  what="Without the fix"
  if [ $failed -gt 0 ]; then
    verdict=":white_check_mark: $failed of ${#TESTS[@]} test(s) fail without the fix, as expected."
  elif [ $passed -gt 0 ]; then
    verdict=":x: The tests pass without the fix, so they do not demonstrate the bug."
    rv=1
  else
    verdict=":x: The tests did not run, so the commit could not be verified."
    rv=1
  fi
else
  what="With the fix"
  if [ $failed -gt 0 ]; then
    verdict=":x: $failed of ${#TESTS[@]} test(s) still fail with the fix."
    rv=1
  elif [ $passed -eq 0 ]; then
    verdict=":x: The tests did not run, so the fix could not be verified."
    rv=1
  else
    verdict=":white_check_mark: All tests pass with the fix."
    [ $skipped -gt 0 ] && verdict="$verdict ($skipped skipped)"
  fi
fi

{
  echo "### $what: $TITLE"
  echo ""
  echo "| Test | Result |"
  echo "|---|---|"
  for t in "${TESTS[@]}"; do
    echo "| ${t#tests/functional/} | ${RESULT[$t]} |"
  done
  echo ""
  echo "$verdict"
  echo ""
} > "$OUT/summary.md"

cat "$OUT/summary.md"
cat "$OUT/summary.md" >> "$GITHUB_STEP_SUMMARY"
if [ $rv != 0 ]; then
  echo "::error::$(echo "$verdict" | sed 's/^:[a-z_]*: //')"
fi
exit $rv
