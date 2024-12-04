#!/usr/bin/env bash

######################################################################
# 9) generate github summary page of all the testings
######################################################################

set -eu

function output() {
  echo -e $* >> "out-$logfile.md"
}

function outfile() {
  cat "$1" >> "out-$logfile.md"
}

function outfile_plain() {
  output "<pre>"
  cat "$1" >> "out-$logfile.md"
  output "</pre>"
}

function send2github() {
  test -f "$1" || exit 0
  dd if="$1" bs=1023k count=1 >> $GITHUB_STEP_SUMMARY
}

# https://docs.github.com/en/enterprise-server@3.6/actions/using-workflows/workflow-commands-for-github-actions#step-isolation-and-limits
# Job summaries are isolated between steps and each step is restricted to a maximum size of 1MiB.
# [ ] can not show all error findings here
# [x] split files into smaller ones and create additional steps

# first call, generate all summaries
if [ ! -f out-1.md ]; then
  logfile="1"
  for tarfile in Logs-functional-*/qemu-*.tar; do
    rm -rf vm* *.txt
    if [ ! -s "$tarfile" ]; then
      output "\n## Functional Tests: unknown\n"
      output ":exclamation: Tarfile $tarfile is empty :exclamation:"
      continue
    fi
    tar xf "$tarfile"
    test -s env.txt || continue
    source env.txt
    # when uname.txt is there, the other files are also ok
    test -s uname.txt || continue
    output "\n## Functional Tests: $OSNAME\n"
    outfile_plain uname.txt
    outfile_plain summary.txt
    outfile failed.txt
    logfile=$((logfile+1))
  done
  send2github out-1.md
else
  send2github out-$1.md
fi
