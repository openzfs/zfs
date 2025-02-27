#!/usr/bin/env perl

# SPDX-License-Identifier: MIT
#
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
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
# This programs converts AEAD test vectors from Project Wycheproof into a
# format that can be consumed more easily by tests/zfs-tests/cmd/crypto_test.
# See tests/zfs-tests/tests/functional/crypto/README for more info.
#

use 5.010;
use warnings;
use strict;
use JSON qw(decode_json);

sub usage {
  say "usage: $0 <infile> [<outfile>]";
  exit 1;
}

my ($infile, $outfile) = @ARGV;

usage() if !defined $infile;

open my $infh, '<', $infile or die "E: $infile: $!\n";
my $json = do { local $/; <$infh> };
close $infh;

my $data = decode_json $json;

select STDERR;

# 0.8 had a slightly different format. 0.9* is current, stabilising for 1.0
my $version = $data->{generatorVersion} // "[unknown]";
if ("$version" !~ m/^0\.9[^0-9]/) {
	warn
	    "W: this converter was written for Wycheproof 0.9 test vectors\n".
	    "     input file has version: $version\n".
	    "   bravely continuing, but expect crashes or garbled output\n";
}

# we only support AEAD tests
my $schema = $data->{schema} // "[unknown]";
if ("$schema" ne 'aead_test_schema.json') {
	warn
	    "W: this converter is expecting AEAD test vectors\n".
	    "     input file has schema: $schema\n".
	    "  bravely continuing, but expect crashes or garbled output\n";
}

# sanity check; algorithm is provided
my $algorithm = $data->{algorithm};
if (!defined $algorithm) {
	die "E: $infile: required field 'algorithm' not found\n";
}

# sanity check; test count is present and correct
my $ntests = 0;
$ntests += $_ for map { scalar @{$_->{tests}} } @{$data->{testGroups}};
if (!exists $data->{numberOfTests}) {
	warn "W: input file has no test count, using mine: $ntests\n";
} elsif ($data->{numberOfTests} != $ntests) {
	warn
	    "W: input file has incorrect test count: $data->{numberOfTests}\n".
	    "   using my own count: $ntests\n";
}

say "  version: $version";
say "   schema: $schema";
say "algorithm: $algorithm";
say "   ntests: $ntests";

my $skipped = 0;

my @tests;

# tests are grouped into "test groups". groups have the same type and IV, key
# and tag sizes. we can infer this info from the tests themselves, but it's
# useful for sanity checks
#
#  "testGroups" : [
#    {
#      "ivSize" : 96,
#      "keySize" : 128,
#      "tagSize" : 128,
#      "type" : "AeadTest",
#      "tests" : [ ... ]
#
for my $group (@{$data->{testGroups}}) {
	# skip non-AEAD test groups
	my $type = $group->{type} // "[unknown]";
	if ($type ne 'AeadTest') {
	    warn "W: group has unexpected type '$type', skipping it\n";
	    $skipped += @{$data->{tests}};
	    next;
	}

	my ($iv_size, $key_size, $tag_size) =
	    @$group{qw(ivSize keySize tagSize)};

	# a typical test:
	#
	# {
	#   "tcId" : 48,
	#   "comment" : "Flipped bit 63 in tag",
	#   "flags" : [
	#     "ModifiedTag"
	#   ],
	#   "key" : "000102030405060708090a0b0c0d0e0f",
	#   "iv" : "505152535455565758595a5b",
	#   "aad" : "",
	#   "msg" : "202122232425262728292a2b2c2d2e2f",
	#   "ct" : "eb156d081ed6b6b55f4612f021d87b39",
	#   "tag" : "d8847dbc326a066988c77ad3863e6083",
	#   "result" : "invalid"
	# },
	#
	# we include everything in the output. the id is useful output so the
	# user can go back to the original test. comment and flags are useful
	# for output in a failing test
	#
	for my $test (@{$group->{tests}}) {
		my ($id, $comment, $iv, $key, $msg, $ct, $aad, $tag, $result) =
		    @$test{qw(tcId comment iv key msg ct aad tag result)};

		# sanity check; iv, key and tag must have the length declared
		# by the group params
		unless (
		    length_check($id, 'iv', $iv, $iv_size) &&
		    length_check($id, 'key', $key, $key_size) &&
		    length_check($id, 'tag', $tag, $tag_size)) {
			$skipped++;
			next;
		}

		# flatten and sort the flags into a single string
		my $flags;
		if ($test->{flags}) {
			$flags = join(' ', sort @{$test->{flags}});
		}

		# the completed test record. we'll emit this later once we're
		# finished with the input; the output file is not open yet.
		push @tests, [
		    [ id => $id ],
		    [ comment => $comment ],
		    (defined $flags ? [ flags => $flags ] : ()),
		    [ iv => $iv ],
		    [ key => $key ],
		    [ msg => $msg ],
		    [ ct => $ct ],
		    [ aad => $aad ],
		    [ tag => $tag ],
		    [ result => $result ],
		];
	}
}

if ($skipped) {
	$ntests -= $skipped;
	warn "W: skipped $skipped tests; new test count: $ntests\n";
}
if ($ntests == 0) {
	die "E: no tests extracted, sorry!\n";
}

my $outfh;
if ($outfile) {
	open $outfh, '>', $outfile or die "E: $outfile: $!\n";
} else {
	$outfh = *STDOUT;
}

# the "header" record has the algorithm and count of tests
say $outfh "algorithm: $algorithm";
say $outfh "tests: $ntests";

#
for my $test (@tests) {
	# blank line is a record separator
	say $outfh "";

	# output the test data in a simple record of 'key: value' lines
	#
	# id: 48
	# comment: Flipped bit 63 in tag
	# flags: ModifiedTag
	# iv: 505152535455565758595a5b
	# key: 000102030405060708090a0b0c0d0e0f
	# msg: 202122232425262728292a2b2c2d2e2f
	# ct: eb156d081ed6b6b55f4612f021d87b39
	# aad:
	# tag: d8847dbc326a066988c77ad3863e6083
	# result: invalid
	for my $row (@$test) {
		my ($k, $v) = @$row;
		say $outfh "$k: $v";
	}
}

close $outfh;

# check that the length of hex string matches the wanted number of bits
sub length_check {
	my ($id, $name, $hexstr, $wantbits) = @_;
	my $got = length($hexstr)/2;
	my $want = $wantbits/8;
	return 1 if $got == $want;
	my $gotbits = $got*8;
	say
	    "W: $id: '$name' has incorrect len, skipping test:\n".
	    "        got $got bytes ($gotbits bits)\n".
	    "        want $want bytes ($wantbits bits)\n";
	return;
}
