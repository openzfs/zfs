#!/usr/bin/env perl

# SPDX-License-Identifier: MIT
#
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
# Copyright (c) 2026, TrueNAS.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

#
# usage: coverage_report.pl tests/unit/test_zap.info
#        coverage_report.pl < tests/unit/test_zap.info
#
# This program takes an lcov/geninfo coverage tracefile and shows a summary
# of line, branch and function coverage for each file. It's focused on the
# specific needs of OpenZFS' unit test suite (see tests/unit/README.md) but
# it should be adaptable to any place where lcov's HTML output is too heavy
# or difficult to use (eg build/CI logs).
#
# The heart of this program is a small parser for the tracefile format as
# described in geninfo(1). The rest is concerned with constructing a useful
# colorised table output.
#

#
# Typical output:
#
# Coverage: test_zap       | By line         | By branch       | By function
#                          | Rate% Total Hit | Rate% Total Hit | Rate% Total Hit
# module/zfs/u8_textprep.c | 42.0%   802 337 | 33.5%   510 171 | 50.0%    12   6
# module/zfs/zap.c         | 52.1%   687 358 | 45.2%   250 113 | 41.1%    90  37
# module/zfs/zap_fat.c     | 87.8%   665 584 | 58.5%   446 261 | 94.6%    37  35
# module/zfs/zap_impl.c    | 81.9%   232 190 | 60.3%   146  88 | 92.0%    25  23
# module/zfs/zap_leaf.c    | 86.7%   466 404 | 69.0%   216 149 | 95.7%    23  22
# module/zfs/zap_micro.c   | 76.5%   238 182 | 54.2%   142  77 | 92.9%    14  13
#

use 5.010;
use warnings;
use strict;
use Cwd qw(getcwd);
use Term::ANSIColor qw(colored);

# Setup for color output. Perl has included Term::ANSIColor since 5.6 (~2000),
# but RGB support didn't arrive until v4 in 5.17.8 (~2012). We disable colors
# outright on versions < 4, or if output is not attached to a terminal.
my $use_colors = -t \*STDOUT && $Term::ANSIColor::VERSION >= 4;

# Palette setup. If Term::ANSIColor and the terminal advertise support for
# it, then we set up a pleasant red -> green gradient for the coverage
# percentages. If not, we scale those colors down to the older RGB-240 colors
# (0-5 for each component), which is still quite nice.
my @palette = !$use_colors ? () : map {
	state $has_truecolor =
	    $Term::ANSIColor::VERSION >= 5 && $ENV{COLORTERM};
	my @rgb = map { hex } m/../g;
	if ($has_truecolor) {
		sprintf 'r%dg%db%d', @rgb;
	} else {
		sprintf 'rgb%d%d%d', map { $_ * 6 / 255 } @rgb;
	}
} (
	# Catppuccin Latte
	# https://catppuccin.com/palette/
	'd20f39',	# Red
	'e64553',	# Maroon
	'fe640b',	# Peach
	'df8e1d',	# Yellow
	'40a02b',	# Green
	'179299',	# Teal
);

# Test name, from the TN: field if present.
my $test_name = '';

# Per-file data, initially sourced from the tracefile, then augmented
my %filedata;

# Tracking for the longest (stringified) value for each key. These are used
# later when computing the output table column width.
my %len;
sub bump_len {
	my ($k, $x) = @_;
	my $l = length "".$x;
	$len{$k} = $l if ($len{$k} // 0) < $l;
}

###
# Parse the tracefile into per-file data records.

# Current working directory. Expected to be the build root. Used to remove
# the leading part of the source filenames, so its not the end of the world
# if its wrong.
my $cwd = getcwd;

# Loop over the input
while (my $line = <>) {
	state $data = {};
	chomp $line;

	# skip comments
	next if $line =~ m/^#/;

	if ($line eq 'end_of_record') {
		# end of this file, prep for next
		$data = {};
		next;
	}

	# everything else should be a KEY:VALUE line
	my ($k, $v) = $line =~ m/^([A-Z]+):(.*)$/;
	unless (defined $k) {
		say "W: $.: malformed line: $line";
		next;
	}

	if ($k eq 'TN') {
		# TN:test_zap

		# Test name. This is actually per-record (a tracefile can
		# carry multiple test results) but we only ever generate
		# them for a single test, so we don't make any effort to
		# notice or track changes.
		$test_name = $v;
		next;
	}

	if ($k eq 'SF') {
		# SF:/home/robn/code/zfs-unit/module/zfs/zap.c

		# Source file. Value is the name, and the rest of the record
		# apply to it.

		# Remove the leading build root name.
		my $path = $v;
		$path =~ s{^$cwd/*}{};

		# If we haven't seen this file before, create a new data
		# record for it.
		$filedata{$v} //= { path => $path };
		$data = $filedata{$v};

		# Increase path column width if necessary.
		bump_len('path', $path);
		next;
	}

	# Handle the counter keys. These are single values for the entire
	# record in the file. L, FN and BR are Line, Function and Branch,
	# F and H are found (ie total) and hit (ie was executed).
	if (grep { $_ eq $k } qw(LF LH FNF FNH BRF BRH)) {
		$data->{lc $k} = $v;
		bump_len(lc $k, $v);
		next;
	}

	# Older versions of lcov may not emit absolute found/hit counters. To
	# handle this, we maintain our own counters from other events recorded
	# in the info file, which we use if we don't get an absolute count.

	if ($k eq 'DA') {
		# DA:<line number>,<execution count>[,<checksum>]
		# DA:463,0
		# DA:469,153
		my ($l, $h) = split ',', $v;

		# One DA: record per actual code line (vs comment or other
		# non-executable line), so we count records, not line number.
		$data->{_lf}++;

		# Only increment the hit count if the line was executed.
		$data->{_lh}++ if $h > 0;
		next;
	}

	if ($k eq 'FN') {
		# FN:<start line>,[<end line>,]<function nname>
		# FN:283,zap_lookup_by_dnode

		# One FN record per function
		$data->{_fnf}++;
		next;
	}
	if ($k eq 'FNDA') {
		# FNDA:<execution count>,<function name>
		# FNDA:0,zap_lookup
		# FNDA:78,zap_lookup_by_dnode

		# Only count hit if more than one execution.
		my ($c) = split ',', $v;
		$data->{_fnh}++ if 0+$c > 0;
		next;
	}

	if ($k eq 'BRDA') {
		# BRDA:<line_number>,[<exception>]<block>,<branch>,<taken>
		# BRDA:365,0,0,-
		# BRDA:365,0,1,-
		my ($l, $b, $br, $c) = split ',', $v;

		# One BRDA: record per branch
		$data->{_brf}++;

		# <taken> is number of times branch arm was taken, or '-' if
		# never considered (eg surrounding block was never entered)
		# they're both 0 for our purposes.
		$c = 0 if $c eq '-';

		# Only count hit if more than one execution.
		$data->{_brh}++ if 0+$c > 0;
		next;
	}
}

###
# Synthesize missing counters

for my $file (keys %filedata) {
	my $data = $filedata{$file};

	for my $k (qw(lf lh fnf fnh brf brh)) {
		# Get our own count, if one exists.
		my $v = delete $data->{"_$k"} // 0;

		# If we didn't find a count in the info file, use our own.
		# Note that this will also set legitimately unseen values to
		# 0 (eg a source file with no branches). That's actually what
		# we want.
		unless (exists $data->{$k}) {
			$data->{$k} = $v;
			bump_len($k, $v);
		}
	}
}

###
# Synthesize the "rate" percentage field from the "found" and "hit" fields.

sub rate {
	my ($data, $k, $kf, $kh) = @_;
	my $rate = sprintf '%.01f%%',
	    $data->{$kf} ? (100 * $data->{$kh} / $data->{$kf}) : 0;
	$data->{$k} = $rate;
	bump_len($k, $rate);
}

for my $file (keys %filedata) {
	my $data = $filedata{$file};
	rate($data, 'lr', 'lf', 'lh');
	rate($data, 'brr', 'brf', 'brh');
	rate($data, 'fnr', 'fnf', 'fnh');
}

###
# Set up the header "rows".

# We reuse our data record structure a little because outputting these needs to
# consider and sometimes contribute to column width.

# The top row spans multiple columns. The pad functions below have extra tools
# to handle the math.
my $h1data = {
	path => 'Coverage'.($test_name ? ": $test_name" : ''),
	l => 'By line',
	br => 'By branch',
	fn => 'By function',
};
bump_len('path', $h1data->{path});

# The second row is the actual header for each data column, and so may push
# the column widths out if necessary.
my $h2data = {
	lr  => 'Rate%', lf  => 'Total', lh  => 'Hit',
	brr => 'Rate%', brf => 'Total', brh => 'Hit',
	fnr => 'Rate%', fnf => 'Total', fnh => 'Hit',
};
bump_len($_, $h2data->{$_}) for keys %$h2data;

###
# Table layout

# Internal helper for padr() and padl() below. The idea is to compute the
# effective column width, and the string we want to place in it. If it would
# fit exactly, we return the string. If not, the passed-in function is called
# with the string, its length and the column width, and it will place it
# (by adding padding on either side).
#
# Most calls take a single column key, which makes it very simple - take
# the max width for that column (from %len, set by bump_len()), and the value
# of that key in this column, and that's all of it.
#
# For the top heading row (h1data above), a list of column keys can be passed
# in. In this case, the string will be constructed as a space-separated list
# of all the keys have have a value in the data row. The column width is the
# sum of max column widths for all columns that mave a max column width, plus
# one for each space separator. This allows us to provide a separate string
# to appear in the space, with the amount of space computed from the columns
# underneath it.
#
sub _pad {
	my ($fn, $data, @k) = @_;
	my $str = join ' ', map { $data->{$_} // () } @k;
	my $strlen = length $str;
	my $colwidth = -1;
	$colwidth += ($len{$_} // -1)+1 for @k;
	return $strlen == $colwidth ? $str : $fn->($str, $strlen, $colwidth);
}

# Return the value of the named fields, with space-padding added to the right.
sub padr {
	_pad(sub {
		my ($str, $strlen, $colwidth) = @_;
		$str . (' ' x ($colwidth - $strlen));
	}, @_);
}

# Return the value of the named fields, with space-padding added to the left.
sub padl {
	_pad(sub {
		my ($str, $strlen, $colwidth) = @_;
		(' ' x ($colwidth - $strlen)) . $str;
	}, @_);
}

# Return the given % string, wrapped in terminal control codes that will give
# it an appropriate color from the palette.
sub colorpct {
	my ($pct) = @_;

	# If colors are disabled, return the string as-is.
	return $pct unless $use_colors;

	my ($n) = $pct =~ m/([0-9\.]+)/;

	# scale 0-100 into palette range
	my $s = int(($#palette / 100) * $n);
	my $c = $palette[$s];

	return colored([$c], $pct);
}

my @rows;

# Layout the first header row
push @rows, [
	padr($h1data, 'path'),
	'|', padr($h1data, 'l', 'lr', 'lf', 'lh'),
	'|', padr($h1data, 'br', 'brr', 'brf', 'brh'),
	'|', padr($h1data, 'fn', 'fnr', 'fnf', 'fnh'),
];

# Layout the second header row
push @rows, [
	padr($h2data, 'path'),
	'|', padr($h2data, 'lr'), padl($h2data, 'lf'), padl($h2data, 'lh'),
	'|', padr($h2data, 'brr'), padl($h2data, 'brf'), padl($h2data, 'brh'),
	'|', padr($h2data, 'fnr'), padl($h2data, 'fnf'), padl($h2data, 'fnh'),
];

# Layout the data rows, padding colorising as appropriate.
for my $file (sort keys %filedata) {
	my $data = $filedata{$file};

	push @rows, [
	    padr($data, 'path'),
	    '|', colorpct(padl($data, 'lr')),
	    padl($data, 'lf'), padl($data, 'lh'),
	    '|', colorpct(padl($data, 'brr')),
	    padl($data, 'brf'), padl($data, 'brh'),
	    '|', colorpct(padl($data, 'fnr')),
	    padl($data, 'fnf'), padl($data, 'fnh'),
	];
}

# And print them all out!
say "@$_" for @rows;
