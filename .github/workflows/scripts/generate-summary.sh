#!/usr/bin/env bash

######################################################################
# generate github summary page of all the testings
######################################################################

function output() {
  echo -e $* >> "out-$logfile.md"
}

function outfile() {
  cat "$1" >> "out-$logfile.md"
}

function send2github() {
  test -f "$1" && dd if="$1" bs=999k count=1 >> $GITHUB_STEP_SUMMARY
}

function error() {
  output ":bangbang: $* :bangbang:\n"
}

# generate summary of one test
function generate() {
  # we issued some error already
  test ! -s log && return

  ######################################################
  # input:
  # - log     -> full debug output
  # - results -> full list with summary in the end
  ######################################################
  # output:
  # - info.txt  -> short summary list (zts-report)
  # - list.txt  -> full list, but without debugging
  # - debug.txt -> full list with debugging info
  ######################################################

  if [ -s results ]; then
    cat results | grep '^Test[: ]' > list.txt
    cat results | grep -v '^Test[: ]' > info.txt
  else
    cat log | grep '^Test[: ]' > list.txt
    ./zts-report.py --no-maybes ./list.txt > info.txt
  fi

  # error details
  awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; }
    /\[SKIP\]|\[PASS\]/{ show=0; } show' log > debug.txt

  # headline of this summary
  output "\n## $headline\n"

  if [ -s uname.txt ]; then
    output "<pre>"
    outfile uname.txt
    output "</pre>"
  fi

  if [ -s info.txt ]; then
    output "<pre>"
    outfile info.txt
    output "</pre>"
  else
    output "All tests passed :thumbsup:"
  fi

  if [ -s dmesg-prerun.txt ]; then
    output "<details><summary>Dmesg - systemstart</summary><pre>"
    outfile dmesg-prerun.txt
    output "</pre></details>"
  fi

  if [ -s dmesg-module-load.txt ]; then
    output "<details><summary>Dmesg - module loading</summary><pre>"
    outfile dmesg-module-load.txt
    output "</pre></details>"
  fi

  if [ -s make-stderr.txt ]; then
    output "<details><summary>Stderr of make</summary><pre>"
    outfile make-stderr.txt
    output "</pre></details>"
  fi

  if [ -s list.txt ]; then
    output "<details><summary>List of all tests</summary><pre>"
    outfile list.txt
    output "</pre></details>"
  fi

  if [ -s debug.txt ]; then
    output "<details><summary>Debug list with dmesg and dbgmsg</summary><pre>"
    outfile debug.txt
    output "</pre></details>"
  fi

  # remove tmp files
  rm -f log results *.txt
  logfile=$((logfile+1))
}

# check tarfiles and untar
function my_untar() {
  if [ -f "$1" ]; then
    tar xf "$1" || error "Tarfile $1 returns some error"
  else
    error "Tarfile $1 not found"
  fi
}

# check file and copy
function my_copy() {
  if [ -f "$1" ]; then
    cat "$1" >> "$2"
  else
    error "File $1 not found"
  fi
}

# sanity checks on ubuntu runner
function summarize_sanity() {
  headline="Sanity Tests Ubuntu $1"
  rm -rf testfiles
  my_untar "Logs-$1-sanity/sanity.tar"
  my_copy "testfiles/log" log
  generate
}

# functional on ubuntu runner matrix
function summarize_functional() {
  headline="Functional Tests Ubuntu $1"
  rm -rf testfiles
  for i in $(seq 1 4); do
    tarfile="Logs-$1-functional-part$i/part$i.tar"
    my_untar "$tarfile"
    my_copy "testfiles/log" log
  done
  generate
}

# functional tests via qemu
function summarize_qemu() {
  for tarfile in Logs-functional*/qemu-*.tar; do
    rm -rf current
    my_untar "$tarfile"
    osname=`cat osname.txt`
    headline="Functional Tests: $osname"
    my_copy "current/log" log
    my_copy "current/results" results
    generate
  done
}

# https://docs.github.com/en/enterprise-server@3.6/actions/using-workflows/workflow-commands-for-github-actions#step-isolation-and-limits
# Job summaries are isolated between steps and each step is restricted to a maximum size of 1MiB.
# [ ] can not show all error findings here
# [x] split files into smaller ones and create additional steps

# first call, generate all summaries
if [ ! -f out-0.md ]; then
  # create ./zts-report.py for generate()
  TEMPLATE="tests/test-runner/bin/zts-report.py.in"
  cat $TEMPLATE| sed -e 's|@PYTHON_SHEBANG@|python3|' > ./zts-report.py
  chmod +x ./zts-report.py

  logfile="0"
  summarize_sanity "20.04"
  summarize_sanity "22.04"
  summarize_functional "20.04"
  summarize_functional "22.04"
  summarize_qemu

  send2github out-0.md
else
  send2github out-$1.md
fi

exit 0
