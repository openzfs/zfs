#!/usr/bin/env bash

######################################################################
# 8) show colored output of results
######################################################################

set -eu

# read our defined variables
source /var/tmp/env.txt
cd $RESPATH

# helper function for showing some content with headline
function showfile() {
  content=$(dd if=$1 bs=1024 count=400k 2>/dev/null)
  if [ -z "$2" ]; then
    group1=""
    group2=""
  else
    SIZE=$(stat --printf="%s" "$file")
    SIZE=$((SIZE/1024))
    group1="##[group]$2 ($SIZE KiB)"
    group2="##[endgroup]"
  fi
cat <<EOF > tmp$$
$group1
$content
$group2
EOF
  cat tmp$$
  rm -f tmp$$
}

# overview
cat /tmp/summary.txt
echo ""

if [ -f /tmp/have_failed_tests -a -s /tmp/failed.txt ]; then
  echo "Debuginfo of failed tests:"
  cat /tmp/failed.txt
  echo ""
  cat /tmp/summary.txt | grep -v '^/'
  echo ""
fi

echo -e "\nFull logs for download:\n    $1\n"

for i in $(seq 1 $VMs); do
  rv=$(cat vm$i/tests-exitcode.txt)

  if [ $rv = 0 ]; then
    vm="[92mvm$i[0m"
  else
    vm="[1;91mvm$i[0m"
  fi

  file="vm$i/dmesg-prerun.txt"
  test -s "$file" && showfile "$file" "$vm: dmesg kernel"

  file="/tmp/vm${i}log.txt"
  test -s "$file" && showfile "$file" "$vm: test results"

  file="vm$i/console.txt"
  test -s "$file" && showfile "$file" "$vm: serial console"

  file="/tmp/vm${i}dbg.txt"
  test -s "$file" && showfile "$file" "$vm: failure logfile"
done

test -f /tmp/have_failed_tests && exit 1
exit 0
