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

use 5.010;
use warnings;
use strict;

# All files known to git are either "tagged" or "untagged". Tagged files are
# expected to have a license tag, while untagged files are expected to _not_
# have a license tag. There is no "optional" tag; all files are either "tagged"
# or "untagged".
#
# Whether or not a file is tagged or untagged is determined using the patterns
# in $tagged_patterns and $untagged_patterns and the following sequence:
#
# - if the file's full path is explicity listed in $tagged_patterns, then the
#   file is tagged.
#
# - if the file's full path is explicitly listed in $untagged_patterns, then
#   file is untagged.
#
# - if the filename matches a pattern in $tagged_patterns, and does not match a
#   pattern in $untagged_patterns, then the file is tagged
#
# - otherwise, the file is untagged.
#
# The patterns do a simple glob-like match over the entire path relative to the
# root of the git repo (no leading /). '*' matches as anything at that point,
# across path fragments. '?' matches a single character.

my $tagged_patterns = q(
	# Compiled source files
	*.c
	*.h
	*.S

	# Python files, eg test suite drivers, libzfs bindings
	*.py
	*.py.in

	# Various support scripts
	*.sh
	*.pl

	# Test suite
	*.ksh
	*.ksh.in
	*.kshlib
	*.kshlib.in
	*.shlib

	# Test suite data files
	*.run
	*.cfg
	*.cfg.in
	*.fio
	*.lua
	*.zcp

	# Manpages
	man/man?/*.?
	man/man?/*.?.in

	# Unsuffixed programs (or generated of same)
	cmd/arcstat.in
	cmd/arc_summary
	cmd/dbufstat.in
	cmd/zilstat.in
	cmd/zpool/zpool.d/*
	etc/init.d/zfs-import.in
	etc/init.d/zfs-load-key.in
	etc/init.d/zfs-mount.in
	etc/init.d/zfs-share.in
	etc/init.d/zfs-zed.in
	etc/zfs/zfs-functions.in

	# Misc items that have clear licensing info but aren't easily matched,
	# or are the first of a class that we aren't ready to match yet.
	config/ax_code_coverage.m4
	configure.ac
	module/lua/README.zfs
	scripts/kmodtool
	tests/zfs-tests/tests/functional/inheritance/README.config
	tests/zfs-tests/tests/functional/inheritance/README.state
	cmd/zed/zed.d/statechange-notify.sh
);

my $untagged_patterns = q(
	# Exclude CI tooling as it's not interesting for overall project
	# licensing.
	.github/*

	# Everything below this has unclear licensing. Work is happening to
	# identify and update them. Once one gains a tag it should be removed
	# from this list.

	cmd/zed/zed.d/*.sh
	cmd/zpool/zpool.d/*

	contrib/coverity/model.c
	include/libzdb.h
	include/os/freebsd/spl/sys/inttypes.h
	include/os/freebsd/spl/sys/mode.h
	include/os/freebsd/spl/sys/trace.h
	include/os/freebsd/spl/sys/trace_zfs.h
	include/os/freebsd/zfs/sys/zpl.h
	include/os/linux/kernel/linux/page_compat.h
	lib/libspl/include/os/freebsd/sys/sysmacros.h
	lib/libspl/include/sys/string.h
	lib/libspl/include/sys/trace_spl.h
	lib/libspl/include/sys/trace_zfs.h
	lib/libzdb/libzdb.c
	module/lua/setjmp/setjmp.S
	module/lua/setjmp/setjmp_ppc.S
	module/zstd/include/sparc_compat.h
	module/zstd/zstd_sparc.c
	tests/zfs-tests/cmd/cp_files.c
	tests/zfs-tests/cmd/zed_fd_spill-zedlet.c
	tests/zfs-tests/tests/functional/tmpfile/tmpfile_001_pos.c
	tests/zfs-tests/tests/functional/tmpfile/tmpfile_002_pos.c
	tests/zfs-tests/tests/functional/tmpfile/tmpfile_003_pos.c
	tests/zfs-tests/tests/functional/tmpfile/tmpfile_test.c

	autogen.sh
	contrib/bpftrace/zfs-trace.sh
	contrib/pyzfs/docs/source/conf.py
	contrib/pyzfs/libzfs_core/test/__init__.py
	contrib/pyzfs/setup.py.in
	contrib/zcp/autosnap.lua
	scripts/commitcheck.sh
	scripts/man-dates.sh
	scripts/mancheck.sh
	scripts/paxcheck.sh
	scripts/zfs-helpers.sh
	scripts/zfs-tests-color.sh
	scripts/zfs.sh
	scripts/zimport.sh
	tests/zfs-tests/callbacks/zfs_failsafe.ksh
	tests/zfs-tests/include/commands.cfg
	tests/zfs-tests/include/tunables.cfg
	tests/zfs-tests/include/zpool_script.shlib
	tests/zfs-tests/tests/functional/mv_files/random_creation.ksh
);

# For files expected to have a license tags, these are the acceptable tags by
# path. A file in one of these paths with a tag not listed here must be in the
# override list below. If the file is not in any of these paths, then
# $default_license_tags is used.
my $default_license_tags = [
    'CDDL-1.0', '0BSD', 'BSD-2-Clause', 'BSD-3-Clause', 'MIT'
];

my @path_license_tags = (
	# Conventional wisdom is that the Linux SPL must be GPL2+ for
	# kernel compatibility.
	'module/os/linux/spl' => ['GPL-2.0-or-later'],
	'include/os/linux/spl' => ['GPL-2.0-or-later'],

	# Third-party code should keep it's original license
	'module/zstd/lib' => ['BSD-3-Clause OR GPL-2.0-only'],
	'module/lua' => ['MIT'],

	# lua/setjmp is platform-specific code sourced from various places
	'module/lua/setjmp' => $default_license_tags,

	# Some of the fletcher modules are dual-licensed
	'module/zcommon/zfs_fletcher' =>
	    ['BSD-2-Clause OR GPL-2.0-only', 'CDDL-1.0'],

	'module/icp' => ['Apache-2.0', 'CDDL-1.0'],

	# Python bindings are always Apache-2.0
	'contrib/pyzfs' => ['Apache-2.0'],
);

# This is a list of "special case" license tags that are in use in the tree,
# and the files where they occur. these exist for a variety of reasons, and
# generally should not be used for new code. If you need to bring in code that
# has a different license from the acceptable ones listed above, then you will
# also need to add it here, with rationale provided and approval given in your
# PR.
my %override_file_license_tags = (

	# SPDX have repeatedly rejected the creation of a tag for a public
	# domain dedication, as not all dedications are clear and unambiguious
	# in their meaning and not all jurisdictions permit relinquishing a
	# copyright anyway.
	#
	# A reasonably common workaround appears to be to create a local
	# (project-specific) identifier to convey whatever meaning the project
	# wishes it to. To cover OpenZFS' use of third-party code with a
	# public domain dedication, we use this custom tag.
	#
	# Further reading:
	#   https://github.com/spdx/old-wiki/blob/main/Pages/Legal%20Team/Decisions/Dealing%20with%20Public%20Domain%20within%20SPDX%20Files.md
	#   https://spdx.github.io/spdx-spec/v2.3/other-licensing-information-detected/
	#   https://cr.yp.to/spdx.html
	#
	'LicenseRef-OpenZFS-ThirdParty-PublicDomain' => [qw(
		include/sys/skein.h
		module/icp/algs/skein/skein_block.c
		module/icp/algs/skein/skein.c
		module/icp/algs/skein/skein_impl.h
		module/icp/algs/skein/skein_iv.c
		module/icp/algs/skein/skein_port.h
		module/zfs/vdev_draid_rand.c
	)],

	# Legacy inclusions
	'Brian-Gladman-3-Clause' => [qw(
		module/icp/asm-x86_64/aes/aestab.h
		module/icp/asm-x86_64/aes/aesopt.h
		module/icp/asm-x86_64/aes/aeskey.c
		module/icp/asm-x86_64/aes/aes_amd64.S
	)],
	'OpenSSL-standalone' => [qw(
		module/icp/asm-x86_64/aes/aes_aesni.S
	)],
	'LGPL-2.1-or-later' => [qw(
		config/ax_code_coverage.m4
	)],

	# Legacy inclusions of BSD-2-Clause files in Linux SPL.
	'BSD-2-Clause' => [qw(
		include/os/linux/spl/sys/debug.h
		module/os/linux/spl/spl-zone.c
	)],

	# Temporary overrides for things that have the wrong license for
	# their path. Work is underway to understand and resolve these.
	'GPL-2.0-or-later' => [qw(
		include/os/freebsd/spl/sys/kstat.h
		include/os/freebsd/spl/sys/sunddi.h
		include/sys/mod.h
	)],
	'CDDL-1.0' => [qw(
		include/os/linux/spl/sys/errno.h
		include/os/linux/spl/sys/ia32/asm_linkage.h
		include/os/linux/spl/sys/misc.h
		include/os/linux/spl/sys/procfs_list.h
		include/os/linux/spl/sys/trace.h
		include/os/linux/spl/sys/trace_spl.h
		include/os/linux/spl/sys/trace_taskq.h
		include/os/linux/spl/sys/wmsum.h
		module/os/linux/spl/spl-procfs-list.c
		module/os/linux/spl/spl-trace.c
		module/lua/README.zfs
	)],
);

##########

sub setup_patterns {
	my ($patterns) = @_;

	my @re;
	my @files;

	for my $pat (split "\n", $patterns) {
		# remove leading/trailing whitespace and comments
		$pat =~ s/(:?^\s*|\s*(:?#.*)?$)//g;
		# skip (now-)empty lines
		next if $pat eq '';

		# if the "pattern" has no metachars, then it's a literal file
		# path and gets matched a bit more strongly
		unless ($pat =~ m/[?*]/) {
			push @files, $pat;
			next;
		}

		# naive pattern to regex conversion

		# escape simple metachars
		$pat =~ s/([\.\(\[])/\Q$1\E/g;

		$pat =~ s/\?/./g;	# glob ? -> regex .
		$pat =~ s/\*/.*/g;	# glob * -> regex .*

		push @re, $pat;
	}

	my $re = join '|', @re;
	return (qr/^(?:$re)$/, { map { $_ => 1 } @files });
};

my ($tagged_re, $tagged_files) = setup_patterns($tagged_patterns);
my ($untagged_re, $untagged_files) = setup_patterns($untagged_patterns);

sub file_is_tagged {
	my ($file) = @_;

	# explicitly tagged
	if ($tagged_files->{$file}) {
		delete $tagged_files->{$file};
		return 1;
	}

	# explicitly untagged
	if ($untagged_files->{$file}) {
		delete $untagged_files->{$file};
		return 0;
	}

	# must match tagged patterns and not match untagged patterns
	return ($file =~ $tagged_re) && !($file =~ $untagged_re);
}

my %override_tags = map {
	my $tag = $_;
	map { $_ => $tag } @{$override_file_license_tags{$_}};
} keys %override_file_license_tags;

##########

my $rc = 0;

# Get a list of all files known to git. This is a crude way of avoiding any
# build artifacts that have tags embedded in them.
my @git_files = sort grep { chomp } qx(git ls-tree --name-only -r HEAD);

# Scan all files and work out if their tags are correct.
for my $file (@git_files) {
	# Ignore non-files. git can store other types of objects (submodule
	# dirs, symlinks, etc) that aren't interesting for licensing.
	next unless -f $file && ! -l $file;

	# Open the file, and extract its license tag. We only check the first
	# 4K of each file because many of these files are large, binary, or
	# both.  For a typical source file that means the tag should be found
	# within the first ~50 lines.
	open my $fh, '<', $file or die "$0: couldn't open $file: $!\n";
	my $nbytes = read $fh, my $buf, 4096;
	die "$0: couldn't read $file: $!\n" if !defined $nbytes;

	my ($tag) =
	    $buf =~ m/\bSPDX-License-Identifier: ([A-Za-z0-9_\-\. ]+)$/smg;

	close $fh;

	# Decide if the file should have a tag at all
	my $tagged = file_is_tagged($file);

	# If no license tag is wanted, there's not much left to do
	if (!$tagged) {
		if (defined $tag) {
			# untagged file has a tag, pattern change required
			say "unexpected license tag: $file";
			$rc = 1;
		}
		next;
	}

	# If a tag is required, but doesn't have one, warn and loop.
	if (!defined $tag) {
		say "missing license tag: $file";
		$rc = 1;
		next;
	}

	# Determine the set of valid license tags for this file. Start with
	# the defaults.
	my $tags = $default_license_tags;

	if ($override_tags{$file}) {
		# File has an explicit override, use it.
		$tags = [delete $override_tags{$file}];
	} else {
		# Work through the path tag sets, taking the set with the
		# most precise match. If no sets match, we fall through and
		# are left with the default set.
		my $matchlen = 0;
		for (my $n = 0; $n < @path_license_tags; $n += 2) {
			my ($path, $t) = @path_license_tags[$n,$n+1];
			if (substr($file, 0, length($path)) eq $path &&
			    length($path) > $matchlen) {
				$tags = $t;
				$matchlen = length($path);
			}
		}
	}

	# Confirm the file's tag is in the set, and warn if not.
	my %tags = map { $_ => 1 } @$tags;
	unless ($tags{$tag}) {
		say "invalid license tag: $file";
		say "    (got $tag; expected: @$tags)";
		$rc = 1;
		next;
	}
}

##########

# List any files explicitly listed as tagged or untagged that we didn't see.
# Likely the file was removed from the repo but not from our lists.

for my $file (sort keys %$tagged_files) {
	say "explicitly tagged file not on disk: $file";
	$rc = 1;
}
for my $file (sort keys %$untagged_files) {
	say "explicitly untagged file not on disk: $file";
	$rc = 1;
}
for my $file (sort keys %override_tags) {
	say "explicitly overridden file not on disk: $file";
	$rc = 1;
}

exit $rc;
