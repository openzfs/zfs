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

PRINTF=${PRINTF:=/usr/bin/printf}

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

typeset -A tests=()


typeset -A tests[01]=()
tests[01][desc]="hexadecimal lowercase"
tests[01][format]='%04x'
tests[01][args]="255"
tests[01][result]="00ff"

typeset -A tests[02]=()
tests[02][desc]="hexadecimal 32-bit" 
tests[02][format]='%08x'
tests[02][args]='65537'
tests[02][result]=00010001

typeset -A tests[03]=()
tests[03][desc]="multiple arguments"
tests[03][format]='%d %s '
tests[03][args]="1 one 2 two 3 three"
tests[03][result]='1 one 2 two 3 three '

typeset -A tests[04]=()
tests[04][desc]="variable position parameters"
tests[04][format]='%2$s %1$d '
tests[04][args]="1 one 2 two 3 three"
tests[04][result]='one 1 two 2 three 3 '

typeset -A tests[05]=()
tests[05][desc]="width"
tests[05][format]='%10s'
tests[05][args]="abcdef"
tests[05][result]='    abcdef'

typeset -A tests[06]=()
tests[06][desc]="width and precision"
tests[06][format]='%10.3s'
tests[06][args]="abcdef"
tests[06][result]='       abc'

typeset -A tests[07]=()
tests[07][desc]="variable width and precision"
tests[07][format]='%*.*s'
tests[07][args]="10 3 abcdef"
tests[07][result]='       abc'

typeset -A tests[08]=()
tests[08][desc]="variable position width and precision"
tests[08][format]='%2$*1$.*3$s'
tests[08][args]="10 abcdef 3"
tests[08][result]='       abc'

typeset -A tests[09]=()
tests[09][desc]="multi variable position width and precision"
tests[09][format]='%2$*1$.*3$s'
tests[09][args]="10 abcdef 3 5 xyz 1"
tests[09][result]='       abc    x'

typeset -A tests[10]=()
tests[10][desc]="decimal from hex"
tests[10][format]='%d '
tests[10][args]="0x1000 0XA"
tests[10][result]='4096 10 '

typeset -A tests[11]=()
tests[11][desc]="negative dec (64-bit)"
tests[11][format]='%x'
tests[11][args]="-1"
tests[11][result]='ffffffffffffffff'

typeset -A tests[12]=()
tests[12][desc]="float (basic)"
tests[12][format]='%f'
tests[12][args]="3.14"
tests[12][result]='3.140000'

typeset -A tests[12]=()
tests[12][desc]="float precision"
tests[12][format]='%.2f'
tests[12][args]="3.14159"
tests[12][result]='3.14'

typeset -A tests[13]=()
tests[13][desc]="left justify"
tests[13][format]='%-5d'
tests[13][args]="45"
tests[13][result]='45   '

typeset -A tests[14]=()
tests[14][desc]="newlines"
tests[14][format]='%s\n%s\n%s'
tests[14][args]="one two three"
tests[14][result]='one
two
three'

typeset -A tests[15]=()
tests[15][desc]="embedded octal escape"
tests[15][format]='%s\41%s'
tests[15][args]="one two"
tests[15][result]='one!two'

typeset -A tests[16]=()
tests[16][desc]="backslash string (%b)"
tests[16][format]='%b'
tests[16][args]='\0101\0102\0103'
tests[16][result]='ABC'

typeset -A tests[17]=()
tests[17][desc]="backslash c in %b"
tests[17][format]='%b%s'
tests[17][args]='\0101\cone two'
tests[17][result]='A'

typeset -A tests[18]=()
tests[18][desc]="backslash octal in format"
tests[18][format]='HI\1120K\0112tabbed\11again'
tests[18][args]=
tests[18][result]='HIJ0K	2tabbed	again'

typeset -A tests[19]=()
tests[19][desc]="backslash octal in %b"
tests[19][format]="%b"
tests[19][args]='HI\0112K\011tabbed'
tests[19][result]='HIJK	tabbed'

typeset -A tests[20]=()
tests[20][desc]="numeric %d and ASCII conversions"
tests[20][format]='%d '
tests[20][args]="3 +3 -3 \"3 \"+ '-"
tests[20][result]='3 3 -3 51 43 45 '

typeset -A tests[21]=()
tests[21][desc]="verify second arg only"
tests[21][format]='%2$s'
tests[21][args]='abc xyz'
tests[21][result]="xyz"

typeset -A tests[22]=()
tests[22][desc]="verify missing signed arg"
tests[22][format]='%d %d'
tests[22][args]='151'
tests[22][result]="151 0"

typeset -A tests[23]=()
tests[23][desc]="verify missing unsigned arg"
tests[23][format]='%u %u'
tests[23][args]='151'
tests[23][result]="151 0"

#debug=yes

for i in "${!tests[@]}"; do
	t=test_$i
	desc=${tests[$i][desc]}
	format=${tests[$i][format]}
	args="${tests[$i][args]}"
	result=${tests[$i][result]}
	
	test_start $t "${tests[$i][desc]}"
	[[ -n "$debug" ]] && echo $PRINTF "$format" "${args[@]}"
	comp=$($PRINTF "$format" ${args[@]})
	checkrv $t 
	[[ -n "$debug" ]] && echo "got [$comp]"
	good=$result
	compare $t "$comp" "$good"
	test_pass $t
done
