#!/usr/bin/env bash

# for runtime reasons we split functional testings into N parts
# - use a define to check for missing tarfiles
FUNCTIONAL_PARTS="4"

ZTS_REPORT="tests/test-runner/bin/zts-report.py"
chmod +x $ZTS_REPORT

function output() {
  echo -e $* >> Summary.md
}

function error() {
  output ":bangbang: $* :bangbang:\n"
}

# this function generates the real summary
# - expects a logfile "log" in current directory
function generate() {
  # we issued some error already
  test ! -s log && return

  # for overview and zts-report
  cat log | grep '^Test' > list

  # error details
  awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; }
    /\[SKIP\]|\[PASS\]/{ show=0; } show' log > err

  # summary of errors
  if [ -s err ]; then
    output "<pre>"
    $ZTS_REPORT --no-maybes ./list >> Summary.md
    output "</pre>"

    # generate seperate error logfile
    ERRLOGS=$((ERRLOGS+1))
    errfile="err-$ERRLOGS.md"
    echo -e "\n## $headline (debugging)\n" >> $errfile
    echo "<details><summary>Error Listing - with dmesg and dbgmsg</summary><pre>" >> $errfile
    dd if=err bs=999k count=1 >> $errfile
    echo "</pre></details>" >> $errfile
  else
    output "All tests passed :thumbsup:"
  fi

  output "<details><summary>Full Listing</summary><pre>"
  cat list >> Summary.md
  output "</pre></details>"

  # remove tmp files
  rm -f err list log
}

# check tarfiles and untar
function check_tarfile() {
  if [ -f "$1" ]; then
    tar xf "$1" || error "Tarfile $1 returns some error"
  else
    error "Tarfile $1 not found"
  fi
}

# check logfile and concatenate test results
function check_logfile() {
  if [ -f "$1" ]; then
    cat "$1" >> log
  else
    error "Logfile $1 not found"
  fi
}

# sanity
function summarize_s() {
  headline="$1"
  output "\n## $headline\n"
  rm -rf testfiles
  check_tarfile "$2/sanity.tar"
  check_logfile "testfiles/log"
  generate
}

# functional
function summarize_f() {
  headline="$1"
  output "\n## $headline\n"
  rm -rf testfiles
  for i in $(seq 1 $FUNCTIONAL_PARTS); do
    tarfile="$2-part$i/part$i.tar"
    check_tarfile "$tarfile"
    check_logfile "testfiles/log"
  done
  generate
}

# https://docs.github.com/en/enterprise-server@3.6/actions/using-workflows/workflow-commands-for-github-actions#step-isolation-and-limits
# Job summaries are isolated between steps and each step is restricted to a maximum size of 1MiB.
# [ ] can not show all error findings here
# [x] split files into smaller ones and create additional steps

ERRLOGS=0
if [ ! -f Summary/Summary.md ]; then
  # first call, we do the default summary (~500k)
  echo -n > Summary.md
  summarize_s "Sanity Tests Ubuntu 20.04" Logs-20.04-sanity
  summarize_s "Sanity Tests Ubuntu 22.04" Logs-22.04-sanity
  summarize_f "Functional Tests Ubuntu 20.04" Logs-20.04-functional
  summarize_f "Functional Tests Ubuntu 22.04" Logs-22.04-functional

  cat Summary.md >> $GITHUB_STEP_SUMMARY
  mkdir -p Summary
  mv *.md Summary
else
  # here we get, when errors where returned in first call
  test -f Summary/err-$1.md && cat Summary/err-$1.md >> $GITHUB_STEP_SUMMARY
fi

exit 0
