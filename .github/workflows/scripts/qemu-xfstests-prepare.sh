#!/usr/bin/env bash

######################################################################
# Collect xfstests results off the test VM, tar them for upload, and
# write a short GitHub job summary. Runs on the runner; always() in the
# workflow so we still grab logs (and the console) on a guest crash.
######################################################################

set -eu

source /var/tmp/env.txt
RES="$RESPATH"            # /var/tmp/test_results
mkdir -p "$RES"

# Pull results + logs off vm1 (best-effort; the VM may have crashed).
rsync -arL zfs@vm1:xfstests/results "$RES/" 2>/dev/null || true
scp zfs@vm1:xfstests/local.config "$RES/" 2>/dev/null || true
scp 'zfs@vm1:/var/tmp/dmesg-*.txt' "$RES/" 2>/dev/null || true
scp 'zfs@vm1:/var/tmp/tests-exitcode.txt' "$RES/" 2>/dev/null || true
cp -f /var/tmp/xfstests-run.log "$RES/" 2>/dev/null || true
# qemu-5-setup.sh already streams the VM serial console to $RES/vm1/console.txt.

TARNAME="xfstests-$OS"
TAR="/tmp/$TARNAME.tar.bz2"
mv "$RES" "$(dirname "$RES")/$TARNAME"
tar cjf "$TAR" -C "$(dirname "$RES")" -h "$TARNAME" || true
mv "$(dirname "$RES")/$TARNAME" "$RES"

# --- Job summary ----------------------------------------------------
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/stdout}"
{
  echo "## xfstests on ${OSNAME:-$OS}"
  echo
  if [ -f "$RES/tests-exitcode.txt" ]; then
    rv=$(cat "$RES/tests-exitcode.txt")
    if [ "$rv" = "0" ]; then
      echo ":thumbsup: \`./check\` exited 0 — all selected tests passed."
    else
      echo ":warning: \`./check\` exited $rv — failures (or a notrun treated as error)."
    fi
  else
    echo ":interrobang: no exit code recorded — the VM may have crashed mid-run."
  fi
  echo

  # xfstests prints a "Failures:" / "Passed all N tests" line near the end of
  # results/check.log. Surface the tail.
  if [ -f "$RES/results/check.log" ]; then
    echo '<details><summary>check.log (tail)</summary>'
    echo
    echo '```'
    tail -n 40 "$RES/results/check.log"
    echo '```'
    echo '</details>'
  fi
} >> "$SUMMARY" 2>/dev/null || true

exit 0
