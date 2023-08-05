#!/usr/bin/env perl

# SPDX-License-Identifier: MIT
#
# Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
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


# This program will update the AUTHORS file to include commit authors that are
# in the git history but are not yet credited.
#
# The CONTRIBUTORS section of the AUTHORS file attempts to be a list of
# individual contributors to OpenZFS, with one name, address and line per
# person. This is good for readability, but does not really leave room for the
# that names and emails on commits from the same individual can be different,
# for all kinds of reasons, not limited to:
#
# - a person might change organisations, and so their email address changes
#
# - a person might be paid to work on OpenZFS for their employer, and then hack
#   on personal projects in the evening, so commits legitimately come from
#   different addresses
#
# - names change for all kinds of reasons
#
# To try and account for this, this program will try to find all the possible
# names and emails for a single contributor, and then select the "best" one to
# add to the AUTHORS file.
#
# The CONTRIBUTORS section of the AUTHORS file is considered the source of
# truth. Once an individual committer is listed in there, that line will not be
# removed regardless of what is discovered in the commit history. However, it
# can't just be _anything_. The name or email still has to match something seen
# in the commit history, so that we're able to undertand that its the same
# contributor.
#
# The bulk of the work is in running `git log` to fetch commit author names and
# emails. For each value, we generate a "slug" to use as an internal id for
# that value, which is mostly just the lowercase of the value with whitespace
# and punctuation removed. Two values with subtle differences can produce the
# same slug, so at this point we also try to keep the "best" pre-slug value as
# the display version. We use this slug to update two maps, one of email->name,
# the other of name->email.
#
# Once collected, we then walk all the emails we've seen and get all the names
# associated with every instance. Then for each of those names, we get all the
# emails associated, and so on until we've seen all the connected names and
# emails. This collection is every possible name and email for an individual
# contributor.
#
# Finaly, we consider these groups, and select the "best" name and email for
# the contributor, and add them to the author tables if they aren't there
# already. Once we've done everyone, we write out a new AUTHORS file, and
# that's the whole job.
#
# This is imperfect! Its necessary for the user to examine the diff and make
# sure its sensible. If it hasn't hooked up right, it may necessary to adjust
# the input data (via .mailmap) or improve the heuristics in this program. It
# took a long time to get into good shape when first written (355 new names
# added to AUTHORS!) but hopefully in the future we'll be running this
# regularly so it doesn't fall so far behind.


use 5.010;
use warnings;
use strict;

# Storage for the "best looking" version of name or email, keyed on slug.
my %display_name;
my %display_email;

# First, we load the existing AUTHORS file. We save everything before
# CONTRIBUTORS: line as-is so we can write it back out to the new file. Then
# we extract name,email pairs from the remainder and store them in a pair of
# hashtables, keyed on slug.
my %authors_name;
my %authors_email;

my @authors_header;

for my $line (do { local (@ARGV) = ('AUTHORS'); <> }) {
	chomp $line;
	state $in_header = 1;
	if ($in_header) {
		push @authors_header, $line;
		$in_header = 0 if $line =~ m/^CONTRIBUTORS:/;
	} else {
		my ($name, $email) = $line =~ m/^\s+(.+)(?= <) <([^>]+)/;
		next unless $name;

		my $semail = email_slug($email);
		my $sname = name_slug($name);

		$authors_name{$semail} = $sname;
		$authors_email{$sname} = $semail;

		# The name/email in AUTHORS is already the "best looking"
		# version, by definition.
		$display_name{$sname} = $name;
		$display_email{$semail} = $email;
	}
}

# Next, we load all the commit authors. and form name<->email mappings, keyed
# on slug. Note that this format is getting the .mailmap-converted form. This
# lets us control the input to some extent by making changes there.
my %git_names;
my %git_emails;

for my $line (reverse qx(git log --pretty=tformat:'%aN:::%aE')) {
	chomp $line;
	my ($name, $email) = $line =~ m/^(.*):::(.*)/;
	next unless $name && $email;

	my $semail = email_slug($email);
	my $sname = name_slug($name);

	$git_names{$semail}{$sname} = 1;
	$git_emails{$sname}{$semail} = 1;

	# Update the "best looking" display value, but only if we don't already
	# have something from the AUTHORS file. If we do, we must not change it.
	if (!$authors_name{email_slug($email)}) {
		update_display_email($email);
	}

	if (!$authors_email{name_slug($name)}) {
		update_display_name($name);
	}
}

# Now collect unique committers by all names+emails we've ever seen for them.
# We start with emails and resolve all possible names, then we resolve the
# emails for those names, and round and round until there's nothing left.
my @committers;
for my $start_email (sort keys %git_names) {
	# it might have been deleted already through a cross-reference
	next unless $git_names{$start_email};

	my %emails;
	my %names;

	my @check_emails = ($start_email);
	my @check_names;
	while (@check_emails || @check_names) {
		while (my $email = shift @check_emails) {
			next if $emails{$email}++;
			push @check_names,
			    sort keys %{delete $git_names{$email}};
		}
		while (my $name = shift @check_names) {
			next if $names{$name}++;
			push @check_emails,
			    sort keys %{delete $git_emails{$name}};
		}
	}

	# A "committer" is the collection of connected names and emails.
	push @committers, [[sort keys %emails], [sort keys %names]];
}

# Now we have our committers, we can work out what to add to AUTHORS.
for my $committer (@committers) {
	my ($emails, $names) = @$committer;

	# If this commiter is already in AUTHORS, we must not touch.
	next if grep { $authors_name{$_} } @$emails;
	next if grep { $authors_email{$_} } @$names;

	# Decide on the "best" name and email to use
	my $email = best_email(@$emails);
	my $name = best_name(@$names);

	$authors_email{$name} = $email;
	$authors_name{$email} = $name;
}

# Now output the new AUTHORS file
open my $fh, '>', 'AUTHORS' or die "E: couldn't open AUTHORS for write: $!\n";
#my $fh = \*STDOUT;
say $fh join("\n", @authors_header, "");
for my $name (sort keys %authors_email) {
	my $cname = $display_name{$name};
	my $cemail = $display_email{email_slug($authors_email{$name})};
	say $fh "    $cname <$cemail>";
}

exit 0;

# "Slugs" are used at the hashtable key for names and emails. They are used to
# making two variants of a value be the "same" for matching. Mostly this is
# to make upper and lower-case versions of a name or email compare the same,
# but we do a little bit of munging to handle some common cases.
#
# Note that these are only used for matching internally; for display, the
# slug will be used to look up the display form.
sub name_slug {
	my ($name) = @_;

	# Remove spaces and dots, to handle differences in initials.
	$name =~ s/[\s\.]//g;

	return lc $name;
}
sub email_slug {
	my ($email) = @_;

	# Remove everything up to and including the first space, and the last
	# space and everything after it.
	$email =~ s/^(.*\s+)|(\s+.*)$//g;

	# Remove the leading userid+ on Github noreply addresses. They're
	# optional and we want to treat them as the same thing.
	$email =~ s/^[^\+]*\+//g if $email =~ m/\.noreply\.github\.com$/;

	return lc $email;
}

sub update_display_name {
	my ($name) = @_;
	my $sname = name_slug($name);

	# For names, "more specific" means "has more non-lower-case characters"
	# (in ASCII), guessing that if a person has gone to some effort to
	# specialise their name in a later commit, they presumably care more
	# about it. If this is wrong, its probably better to add a .mailmap
	# entry.

	my $cname = $display_name{$sname};
	if (!$cname ||
	    ($name =~ tr/a-z //) < ($cname =~ tr/a-z //)) {
		$display_name{$sname} = $name;
	}
}
sub update_display_email {
	my ($email) = @_;
	my $semail = email_slug($email);

	# Like names, we prefer uppercase when possible. We also remove any
	# leading "plus address" for Github noreply addresses.
	$email =~ s/^[^\+]*\+//g if $email =~ m/\.noreply\.github\.com$/;

	my $cemail = $display_email{$semail};
	if (!$cemail ||
	    ($email =~ tr/a-z //) < ($cemail =~ tr/a-z //)) {
		$display_email{$semail} = $email;
	}
}

sub best_name {
	my @names = sort {
		my $cmp;
		my ($aa) = $display_name{$a};
		my ($bb) = $display_name{$b};

		# The "best" name is very subjective, and a simple sort
		# produced good-enough results, so I didn't try harder. Use of
		# accented characters, punctuation and caps are probably an
		# indicator of "better", but possibly we should also take into
		# account the most recent name we saw, in case the committer
		# has changed their name or nickname or similar.
		#
		# Really, .mailmap is the place to control this.

		return ($aa cmp $bb);
	} @_;

	return shift @names;
}
sub best_email {
	state $internal_re = qr/\.(?:internal|local|\(none\))$/;
	state $noreply_re  = qr/\.noreply\.github\.com$/;
	state $freemail_re = qr/\@(?:gmail|hotmail)\.com$/;

	my @emails = sort {
		my $cmp;

		# prefer address with a single @ over those without
		$cmp = (($b =~ tr/@//) == 1) <=> (($a =~ tr/@//) == 1);
		return $cmp unless $cmp == 0;

		# prefer any address over internal/local addresses
		$cmp = (($a =~ $internal_re) <=> ($b =~ $internal_re));
		return $cmp unless $cmp == 0;

		# prefer any address over github noreply aliases
		$cmp = (($a =~ $noreply_re) <=> ($b =~ $noreply_re));
		return $cmp unless $cmp == 0;

		# prefer any address over freemail providers
		$cmp = (($a =~ $freemail_re) <=> ($b =~ $freemail_re));
		return $cmp unless $cmp == 0;

		# alphabetical by domain
		my ($alocal, $adom) = split /\@/, $a;
		my ($blocal, $bdom) = split /\@/, $b;
		$cmp = ($adom cmp $bdom);
		return $cmp unless $cmp == 0;

		# alphabetical by local part
		return ($alocal cmp $blocal);
	} @_;

	return shift @emails;
}
