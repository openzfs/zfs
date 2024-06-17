#!/usr/bin/env bash

######################################################################
# 8) generate github summary page of all the testings
######################################################################

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

function send2github() {
  test -f "$1" || exit 0
  dd if="$1" bs=1023k count=1 >> $GITHUB_STEP_SUMMARY
}

# generate summary of one test
function generate() {
  osname=`cat osname.txt`
  VMs=`cat vms.txt`

  logfile=$((logfile+1))
  output "\n## Functional Tests: $osname\n"
  for i in `seq 1 $VMs`; do
    for f in uname.txt; do
      test -s vm$i/$f && cat vm$i/$f >> $f
      touch $f
    done
  done

  output "<pre>"
  outfile uname.txt
  output "</pre>"

  if [ -s "summary-clean.txt" ]; then
    output "<pre>"
    outfile "summary-clean.txt"
    output "</pre>"
  fi
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
  send2github out-1.md
else
  send2github out-$1.md
fi
