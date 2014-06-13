#! /usr/bin/ksh
#
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2014 Garrett D'Amore <garrett@damore.org>
#

XARGS=${XARGS:=/usr/bin/xargs}

test_start() {
	print "TEST STARTING ${1}: ${2}"
}

test_pass() {
	print "TEST PASS: ${1}"
}

test_fail() {
	print "TEST FAIL: ${1}: ${2}"
	exit -1
}

checkrv() {
	if [[ $? -ne 0 ]]; then
		test_fail $1 "exit failure"
	fi
}

compare() {
	if [[ "$2" != "$3" ]]; then
		test_fail $1 "compare mismatch, got [$2] expected [$3]"
	fi
}

test1() {
	t=test1
	test_start $t "-I handling"
	comp=$(echo foo bar baz other | $XARGS -I THING echo '** THING **')
	checkrv $t
	good='** foo bar baz other **'
	compare $t "$comp" "$good"
	test_pass $t
}

test2() {
	t=test2
	test_start $t "-n 1 handling"
	comp=$(echo foo bar baz other | $XARGS -n 1 echo '***')
	checkrv $t
	good='*** foo
*** bar
*** baz
*** other'
	compare $t "$comp" "$good"
	test_pass $t
}

test3() {
	t=test3
	test_start $t "-I before -n 1"
	comp=$(echo foo bar baz other | $XARGS -I THING -n1 echo '** THING **')
	checkrv $t
	good='** THING ** foo
** THING ** bar
** THING ** baz
** THING ** other'
	compare $t "$comp" "$good"
	test_pass $t
}

test4() {
	t=test4
	test_start $t "-n 1  before -I"
	comp=$(echo foo bar baz other | $XARGS -n 1 -I THING echo '** THING **')
	checkrv $t
	good='** foo bar baz other **'
	compare $t "$comp" "$good"
	test_pass $t
}

test5() {
	t=test5
	test_start $t "-i multiple lines handling"
	comp=$(printf "abc def\nxyz\n123" | $XARGS -n1 -i echo '[{}]')
	checkrv $t
	good='[abc def]
[xyz]
[123]'
	compare $t "$comp" "$good"
	test_pass $t
}

test6() {
	t=test6
	test_start $t "-E handling"
	comp=$(printf "abc def xyx\n_\n123\nnope" | $XARGS -edef echo)
	checkrv $t
	good='abc'
	compare $t "$comp" "$good"
	test_pass $t
}

test7() {
	t=test7
	test_start $t "newlines in arguments"
	comp=$(printf "abc def\nxyz\n\n123 456\n789" | $XARGS echo)
	checkrv $t
	good='abc def xyz 123 456 789'
	compare $t "$comp" "$good"
	test_pass $t
}

test8() {
	t=test8
	test_start $t "limited counts via -n3"
	comp=$(printf "abc def ghi jkl mno 123 456 789" | $XARGS -n 3 echo '**' )
	checkrv $t
	good='** abc def ghi
** jkl mno 123
** 456 789'
	compare $t "$comp" "$good"
	test_pass $t
}

test9() {
	t=test9
	test_start $t "multiple lines via -L2"
	comp=$(printf "abc def\n123 456\npeterpiper" | $XARGS -L2 echo '**')
	checkrv $t
	good='** abc def 123 456
** peterpiper'
	compare $t "$comp" "$good"
	test_pass $t
}

test10() {
	t=test10
	test_start $t "argument sizes"
	comp=$(printf "abc def 123 456 peter alpha\n" | $XARGS -s15 echo)
	checkrv $t
	good='abc def
123 456
peter
alpha'
	compare $t "$comp" "$good"
	test_pass $t
}

test11() {
	t=test11
	test_start $t "bare -e"
	comp=$(printf "abc def _ end of string" | $XARGS -e echo '**')
	checkrv $t
	good='** abc def _ end of string'
	compare $t "$comp" "$good"
	test_pass $t
}

test12() {
	t=test12
	test_start $t "-E ''"
	comp=$(printf "abc def _ end of string" | $XARGS -E '' echo '**')
	checkrv $t
	good='** abc def _ end of string'
	compare $t "$comp" "$good"
	test_pass $t
}

test13() {
	t=test13
	test_start $t "end of string (no -E or -e)"
	comp=$(printf "abc def _ end of string" | $XARGS echo '**')
	checkrv $t
	good='** abc def'
	compare $t "$comp" "$good"
	test_pass $t
}

test14() {
	t=test14
	test_start $t "trailing blank with -L"
	comp=$(printf "abc def \n123 456\npeter\nbogus" | $XARGS -L2 echo '**')
	checkrv $t
	good='** abc def 123 456 peter
** bogus'
	compare $t "$comp" "$good"
	test_pass $t
}

test15() {
	t=test15
	test_start $t "leading and embedded blanks with -i"
	comp=$(printf "abc def\n  xyz  bogus\nnext" | $XARGS -i echo '** {}')
	checkrv $t
	good='** abc def
** xyz  bogus
** next'
	compare $t "$comp" "$good"
	test_pass $t
}

test16() {
	t=test16
	test_start $t "single character replstring"
	comp=$(echo foo bar baz other | $XARGS -I X echo '** X **')
	checkrv $t
	good='** foo bar baz other **'
	compare $t "$comp" "$good"
	test_pass $t
}

test17() {
	t=test17
	test_start $t "null byte separators"
	comp=$(print 'foo bar baz\000more data' | $XARGS -n1 -0 echo '**')
	checkrv $t
	good='** foo bar baz
** more data'
	compare $t "$comp" "$good"
	test_pass $t
}

test18() {
	t=test18
	test_start $t "escape characters"
	comp=$(printf 'foo\\ bar second" "arg third' | $XARGS -n1 echo '**')
	checkrv $t
	good='** foo bar
** second arg
** third'
	compare $t "$comp" "$good"
	test_pass $t
}

test1
test2
test3
test4
test5
test6
test7
test8
test9
test10
test11
test12
test13
test14
test15
test16
test17
test18
