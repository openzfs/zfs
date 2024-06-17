#!/bin/awk -f
#
# Merge multiple ZTS tests results summaries into a single summary.  This is
# needed when you're running different parts of ZTS on different tests
# runners or VMs.
#
# Usage:
#
#	./merge_summary.awk summary1.txt [summary2.txt] [summary3.txt] ...
#
#	or:
#
#	cat summary*.txt | ./merge_summary.awk
#
BEGIN {
	i=-1
	pass=0
	fail=0
	skip=0
	state=""
	cl=0
	el=0
	upl=0
	ul=0

	# Total seconds of tests runtime
	total=0;
}

# Skip empty lines
/^\s*$/{next}

# Skip Configuration and Test lines
/^Test:/{state=""; next}
/Configuration/{state="";next}

# When we see "test-runner.py" stop saving config lines, and
# save test runner lines
/test-runner.py/{state="testrunner"; runner=runner$0"\n"; next}

# We need to differentiate the PASS counts from test result lines that start
# with PASS, like:
#
#   PASS mv_files/setup
#
# Use state="pass_count" to differentiate
#
/Results Summary/{state="pass_count"; next}
/PASS/{ if (state=="pass_count") {pass += $2}}
/FAIL/{ if (state=="pass_count") {fail += $2}}
/SKIP/{ if (state=="pass_count") {skip += $2}}
/Running Time/{
	state="";
	running[i]=$3;
	split($3, arr, ":")
	total += arr[1] * 60 * 60;
	total += arr[2] * 60;
	total += arr[3]
	next;
}

/Tests with results other than PASS that are expected/{state="expected_lines"; next}
/Tests with result of PASS that are unexpected/{state="unexpected_pass_lines"; next}
/Tests with results other than PASS that are unexpected/{state="unexpected_lines"; next}
{
	if (state == "expected_lines") {
		expected_lines[el] = $0
		el++
	}

	if (state == "unexpected_pass_lines") {
		unexpected_pass_lines[upl] = $0
		upl++
	}
	if (state == "unexpected_lines") {
		unexpected_lines[ul] = $0
		ul++
	}
}

# Reproduce summary
END {
	print runner;
	print "\nResults Summary"
	print "PASS\t"pass
	print "FAIL\t"fail
	print "SKIP\t"skip
	print ""
	print "Running Time:\t"strftime("%T", total, 1)
	if (pass+fail+skip > 0) {
		percent_passed=(pass/(pass+fail+skip) * 100)
	}
	printf "Percent passed:\t%3.2f%", percent_passed

	print "\n\nTests with results other than PASS that are expected:"
	asort(expected_lines, sorted)
	for (j in sorted)
		print sorted[j]

	print "\n\nTests with result of PASS that are unexpected:"
	asort(unexpected_pass_lines, sorted)
	for (j in sorted)
		print sorted[j]

	print "\n\nTests with results other than PASS that are unexpected:"
	asort(unexpected_lines, sorted)
	for (j in sorted)
		print sorted[j]
}
