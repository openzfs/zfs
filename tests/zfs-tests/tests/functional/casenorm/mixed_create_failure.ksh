#!/bin/ksh -p
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
# Copyright 2018 Nutanix Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/casenorm/casenorm.kshlib

# DESCRIPTION:
# For the filesystem with casesensitivity=mixed, normalization=none,
# when multiple files with the same name (differing only in case) are created,
# the number of files is limited to what can fit in a fatzap leaf-block.
# And beyond that, it fails with ENOSPC.
#
# Ensure that the create/rename operations fail gracefully and not trigger an
# ASSERT.
#
# STRATEGY:
# Repeat the below steps for objects: files, directories, symlinks and hardlinks
# 1. Create objects with same name but varying in case.
#    E.g. 'abcdefghijklmnop', 'Abcdefghijklmnop', 'ABcdefghijklmnop' etc.
#    The create should fail with ENOSPC.
# 2. Create an object with name 'tmp_obj' and try to rename it to name that we
#    failed to add in step 1 above.
#    This should fail as well.

verify_runnable "global"

function cleanup
{
        destroy_testfs
}

log_onexit cleanup
log_assert "With mixed mode: ensure create fails with ENOSPC beyond a certain limit"

create_testfs "-o casesensitivity=mixed -o normalization=none"

# Different object types
obj_type=('file' 'dir' 'symlink' 'hardlink')

# Commands to create different object types
typeset -A ops
ops['file']='touch'
ops['dir']='mkdir'
ops['symlink']='ln -s'
ops['hardlink']='ln'

# This function tests the following for a give object type :
# - Create multiple objects with the same name (varying only in case).
#   Ensure that it eventually fails once the leaf-block limit is exceeded.
# - Create another object with a different name. And attempt rename it to the
#   name (for which the create had failed in the previous step).
#   This should fail as well.
# Args :
#   $1 - object type (file/dir/symlink/hardlink)
#   $2 - test directory
#
function test_ops
{
	typeset obj_type=$1
	typeset testdir=$2

	target_obj='target-file'

	op="${ops[$obj_type]}"

	log_note "The op : $op"
	log_note "testdir=$testdir obj_type=$obj_type"

	test_path="$testdir/$obj_type"
	mkdir $test_path
	log_note "Created test dir $test_path"

	if [[ $obj_type = "symlink" || $obj_type = "hardlink" ]]; then
		touch $test_path/$target_obj
		log_note "Created target: $test_path/$target_obj"
		op="$op $test_path/$target_obj"
	fi

	log_note "op : $op"
	names='{a,A}{b,B}{c,C}{d,D}{e,E}{f,F}{g,G}{h,H}{i,I}{j,J}{k,K}{l,L}'
	for name in $names; do
		cmd="$op $test_path/$name"
		out=$($cmd 2>&1)
		ret=$?
		log_note "cmd: $cmd ret: $ret out=$out"
		if (($ret != 0)); then
			if [[ $out = *@(No space left on device)* ]]; then
				save_name="$test_path/$name"
				break;
			else
				log_err "$cmd failed with unexpected error : $out"
			fi
		fi
	done

	log_note 'Test rename \"sample_name\" rename'
	TMP_OBJ="$test_path/tmp_obj"
	cmd="$op $TMP_OBJ"
	out=$($cmd 2>&1)
	ret=$?
	if (($ret != 0)); then
		log_err "cmd:$cmd failed out:$out"
	fi

	# Now, try to rename the tmp_obj to the name which we failed to add earlier.
	# This should fail as well.
	out=$(mv $TMP_OBJ $save_name 2>&1)
	ret=$?
	if (($ret != 0)); then
		if [[ $out = *@(No space left on device)* ]]; then
			log_note "$cmd failed as expected : $out"
		else
			log_err "$cmd failed with : $out"
		fi
	fi
}

for obj_type in ${obj_type[*]};
do
	log_note "Testing create of $obj_type"
	test_ops $obj_type $TESTDIR
done

log_pass "Mixed mode FS: Ops on large number of colliding names fail gracefully"
