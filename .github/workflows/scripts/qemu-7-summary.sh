#!/usr/bin/env bash

######################################################################
# 7) output the prepared results
######################################################################

# max size in KiB of debug output
DEBUG_MAX="100"

set -o pipefail

cd /var/tmp/test_results
OS=`cat os.txt`
RV=0

# helper function for showing some content with headline
function showfile() {
  content=`dd if=$1 bs=1024 count=$DEBUG_MAX 2>/dev/null`
  hl="$2"
  SIZE=`stat --printf="%s" "$file"`
  SIZE=$((SIZE/1024))
  kb=" ($SIZE KiB)"
cat <<EOF > tmp$$
##[group]$hl${kb}
$content
##[endgroup]
EOF
  cat tmp$$
  rm -f tmp$$
}

# overview
cat summary.txt
echo ""

echo "Full logs for download: $1"
echo ""

echo "File listing:"
ls -l

echo ""
file="build-stderr.txt"
test -s "$file" && showfile "$file" "Stderr of module build"

# all other logs are only generated with the testings
test -s vms.txt || exit 1

# build was okay
VMs=`cat vms.txt`

# Did we have a test failure?
if grep -vq 0 vm*/exitcode.txt; then
  echo ""
  file="summary-failure-logs.txt"

  showfile "$file" "One or more tests failed, debug file"
  echo ""

  cat summary.txt
  echo ""

  echo "Full logs for download: $1"
  RV=1
fi

for i in `seq 1 $VMs`; do
  file="vm$i/dmesg-prerun.txt"
  test -s "$file" && showfile "$file" "vm$i: dmesg kernel"

  file="vm$i/console.txt"
  test -s "$file" && showfile "$file" "vm$i: serial console"

  file="vm${i}log.txt.color"
  test -s "$file" && showfile "$file" "vm$i: test results"
done

exit $RV
