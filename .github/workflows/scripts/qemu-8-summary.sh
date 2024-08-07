#!/usr/bin/env bash

######################################################################
# generate github summary page of all the testings
######################################################################

# max size in KiB of debug output
DEBUG_MAX="100"

function output() {
  echo -e $* >> "out-$logfile.md"
}

function outfile() {
  CUR=`stat --printf="%s" "out-$logfile.md"`
  ADD=`stat --printf="%s" "$1"`
  X=$((CUR+ADD))
  if [ $X -gt $((1024*1023)) ]; then
    logfile=$((logfile+1))
  fi
  cat "$1" >> "out-$logfile.md"
}

function showfile() {
  cont=`cat $1`
  hl="$2"
cat <<EOF > tmp
<details>

<summary>$hl</summary>

<pre>$cont</pre>

</details>
EOF
  outfile tmp
  rm -f tmp
}

function send2github() {
  test -f "$1" || exit 0
  dd if="$1" bs=1023k count=1 >> $GITHUB_STEP_SUMMARY
}

function check_copy() {
  test -s "$1" || return 0
  cat $1 > $2
}

function check_append() {
  test -s "$1" || return 0
  cat $1 >> $2
}

# generate summary of one test
function generate() {
  ####################################################################
  # osname.txt                       -> used for headline
  # vm{1,2,3}log.txt                 -> used for summary
  ####################################################################
  # vm{1,2,3}/console.txt            -> all 3 used
  # vm{1,2,3}/uname.txt              -> all 3 used
  # vm{1,2,3}/build-stderr.txt       -> used once
  # vm{1,2,3}/dmesg-prerun.txt       -> used once
  ####################################################################
  # vm{1,2,3}/current/log     -> if not there, kernel panic loading
  # vm{1,2,3}/current/results -> if not there, kernel panic testings
  # vm{1,2,3}/exitcode.txt
  ####################################################################

  # headline of this summary
  logfile=$((logfile+1))
  osname=`cat osname.txt`
  VMs=`cat vms.txt`

  output "\n# Functional Tests: $osname\n"
  for i in `seq 1 $VMs`; do
    for f in build-stderr.txt dmesg-prerun.txt; do
      check_copy vm$i/$f $f
      touch $f
    done
    for f in uname.txt; do
      check_append vm$i/$f $f
      touch $f
    done
  done

  output "<pre>"
  outfile uname.txt
  output "</pre>"

  showfile "build-stderr.txt" "Module build (stderr output)"
  showfile "dmesg-prerun.txt" "Dmesg output (vm start and module load)"

  for i in `seq 1 $VMs`; do
    log="vm$i/current/log"
    console="vm$i/console.txt"

    FAIL=0
    if [ ! -s "$log" ]; then
      output ":exclamation: Logfile of vm$i is missing :exclamation:"
      FAIL=1
    fi

    if [ ! -s "vm$i/current/results" ]; then
      output ":exclamation: Results file of vm$i is missing :exclamation:"
      FAIL=1
    fi

    test -s $console && showfile "$console" "vm$i: serial console output"
    # no log or resultfile, skip presenting them
    test $FAIL != 0 && continue

    awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; } \
      /\[SKIP\]|\[PASS\]/{ show=0; } show' $log > debug.txt
    S=`stat --printf="%s" "debug.txt"`
    if [ $S -gt $((1024*DEBUG_MAX)) ]; then
      dd if=debug.txt of=debug-vm$i.txt count=$DEBUG_MAX bs=1024 2>/dev/null
      echo "..." >> debug-vm$i.txt
      echo "!!! THIS FILE IS BIGGER !!!" >> debug-vm$i.txt
      echo "Please download the zip archiv for full content!" >> debug-vm$i.txt
    else
      mv -f debug.txt debug-vm$i.txt
    fi
  done

  # /home/runner/work/zfs/zfs/.github/workflows/scripts/merge_summary.awk
  BASE="$HOME/work/zfs/zfs"
  MERGE="$BASE/.github/workflows/scripts/merge_summary.awk"
  $MERGE vm*log.txt > merge.txt
  test -s "merge.txt" && showfile "merge.txt" "Test Summary"

  for f in debug-vm*.txt; do
    test -s $f || continue
    S=`stat --printf="%s" "$f"`
    KB=`echo "$S/1024"|bc`
    showfile "$f" "Debug file $f: $KB KiB"
  done
}

# functional tests via qemu
function summarize() {
  for tarfile in Logs-functional-*/qemu-*.tar; do
    if [ ! -s "$tarfile" ]; then
      output "\n# Functional Tests: unknown\n"
      output ":exclamation: Tarfile $tarfile is empty :exclamation:"
      continue
    fi
    rm -rf vm* *.txt
    tar xf "$tarfile"
    generate
  done
}

# https://docs.github.com/en/enterprise-server@3.6/actions/using-workflows/workflow-commands-for-github-actions#step-isolation-and-limits
# Job summaries are isolated between steps and each step is restricted to a maximum size of 1MiB.
# [ ] can not show all error findings here
# [x] split files into smaller ones and create additional steps

# first call, generate all summaries
if [ ! -f out-1.md ]; then
  # create ./zts-report.py for generate()
  TEMPLATE="tests/test-runner/bin/zts-report.py.in"
  cat $TEMPLATE| sed -e 's|@PYTHON_SHEBANG@|python3|' > ./zts-report.py
  chmod +x ./zts-report.py

  logfile="0"
  summarize
  send2github out-1.md
else
  send2github out-$1.md
fi

exit 0
