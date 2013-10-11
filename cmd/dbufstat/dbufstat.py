#!/usr/bin/python
#
# Print out statistics for all cached dmu buffers.  This information
# is available through the dbufs kstat and may be post-processed as
# needed by the script.
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (C) 2013 Lawrence Livermore National Security, LLC.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#

import sys
import getopt
import errno

def usage():
	sys.stderr.write("Usage: dbufstat.py [--help] "
			 "[--buffers] [--objects] [--types] [file]\n\n")
	sys.stderr.write("\t --help    : Print this help message\n")
	sys.stderr.write("\t --buffers : Print minimally formatted "
			 "information for each dbuf\n")
	sys.stderr.write("\t --objects : Print table of "
			 "information for each unique object\n")
	sys.stderr.write("\t --types   : Print table of "
			 "information for each dnode type\n")
	sys.stderr.write("\nExamples:\n")
	sys.stderr.write("\tdbufstat.py\n")
	sys.stderr.write("\tdbufstat.py --buffers\n")
	sys.stderr.write("\tdbufstat.py --objects /tmp/dbufs.log\n")
	sys.stderr.write("\n")

	sys.exit(1)

def get_typestring(t):
	type_strings = ["DMU_OT_NONE",
		# general:
		"DMU_OT_OBJECT_DIRECTORY", "DMU_OT_OBJECT_ARRAY",
		"DMU_OT_PACKED_NVLIST", "DMU_OT_PACKED_NVLIST_SIZE",
		"DMU_OT_BPOBJ", "DMU_OT_BPOBJ_HDR",
		# spa:
		"DMU_OT_SPACE_MAP_HEADER", "DMU_OT_SPACE_MAP",
		# zil:
		"DMU_OT_INTENT_LOG",
		# dmu:
		"DMU_OT_DNODE", "DMU_OT_OBJSET",
		# dsl:
		"DMU_OT_DSL_DIR", "DMU_OT_DSL_DIR_CHILD_MAP",
		"DMU_OT_DSL_DS_SNAP_MAP", "DMU_OT_DSL_PROPS",
		"DMU_OT_DSL_DATASET",
		# zpl:
		"DMU_OT_ZNODE", "DMU_OT_OLDACL", "DMU_OT_PLAIN_FILE_CONTENTS",
		"DMU_OT_DIRECTORY_CONTENTS", "DMU_OT_MASTER_NODE",
		"DMU_OT_UNLINKED_SET",
		# zvol:
		"DMU_OT_ZVOL", "DMU_OT_ZVOL_PROP",
		# other; for testing only!
		"DMU_OT_PLAIN_OTHER", "DMU_OT_UINT64_OTHER", "DMU_OT_ZAP_OTHER",
		# new object types:
		"DMU_OT_ERROR_LOG", "DMU_OT_SPA_HISTORY",
		"DMU_OT_SPA_HISTORY_OFFSETS", "DMU_OT_POOL_PROPS",
		"DMU_OT_DSL_PERMS", "DMU_OT_ACL", "DMU_OT_SYSACL",
		"DMU_OT_FUID", "DMU_OT_FUID_SIZE", "DMU_OT_NEXT_CLONES",
		"DMU_OT_SCAN_QUEUE", "DMU_OT_USERGROUP_USED",
		"DMU_OT_USERGROUP_QUOTA", "DMU_OT_USERREFS", "DMU_OT_DDT_ZAP",
		"DMU_OT_DDT_STATS", "DMU_OT_SA", "DMU_OT_SA_MASTER_NODE",
		"DMU_OT_SA_ATTR_REGISTRATION", "DMU_OT_SA_ATTR_LAYOUTS",
		"DMU_OT_SCAN_XLATE", "DMU_OT_DEDUP", "DMU_OT_DEADLIST",
		"DMU_OT_DEADLIST_HDR", "DMU_OT_DSL_CLONES",
		"DMU_OT_BPOBJ_SUBOBJ"]

	try:
		return type_strings[t];
	except IndexError:
		return "%i" % t


def objects_update_dict(d, pool, objset, objnum, level, blkid, bufsize,
			objtype, btype, data_bs, meta_bs, bsize, lvls, holds,
			blocks, objsize):
	if pool not in d:
		d[pool] = dict()

	if objset not in d[pool]:
		d[pool][objset] = dict()

	if objnum not in d[pool][objset]:
		d[pool][objset][objnum] = dict({'type'     : objtype,
						'btype'    : btype,
						'data_bs'  : data_bs,
						'meta_bs'  : meta_bs,
						'bsize'    : bsize,
						'lvls'     : lvls,
						'holds'    : holds,
						'blocks'   : blocks,
						'size'     : objsize,
						'cached'   : 0,
						'direct'   : 0,
						'indirect' : 0,
						'bonus'    : 0,
						'spill'    : 0})

	d[pool][objset][objnum]['cached'] += bufsize

	if blkid == -1:
		d[pool][objset][objnum]['bonus'] += bufsize
	elif blkid == -2:
		d[pool][objset][objnum]['spill'] += bufsize
	else:
		if level == 0:
			d[pool][objset][objnum]['direct'] += bufsize
		else:
			d[pool][objset][objnum]['indirect'] += bufsize

	return d

def objects_build_dict(filehandle):
	d = dict()

	# First 3 lines are header information, skip these lines.
	for i in range(0, 3):
		next(filehandle)

	for line in filehandle:
		cols = line.split()

		objects_update_dict(d, cols[0], int(cols[1]), int(cols[2]),
				    int(cols[3]), int(cols[4]), int(cols[6]),
				    int(cols[28]), int(cols[29]), int(cols[30]),
				    int(cols[31]), int(cols[32]), int(cols[33]),
				    int(cols[34]), int(cols[35]), int(cols[36]))

	return d

def objects_print_dict_object(d, pool, objset, objnum):
	o = d[pool][objset][objnum]
	t = get_typestring(o['type'])

	try:
		sys.stdout.write("%-16s %-8i %-8i | %-28s %-6i %-8i %-8i "
				 "%-6i %-6i %-5i %-8i %-12i | %-12i %-12i "
				 "%-12i %-8i %-8i\n" % \
				 (pool, objset, objnum, t, o['btype'],
				  o['data_bs'], o['meta_bs'], o['bsize'],
				  o['lvls'], o['holds'], o['blocks'],
				  o['size'], o['cached'], o['direct'],
				  o['indirect'], o['bonus'], o['spill']))
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def objects_print_dict(d):
	try:
		sys.stdout.write("%-16s %-8s %-8s | %-28s %-6s %-8s %-8s "
				 "%-6s %-6s %-5s %-8s %-12s | %-12s %-12s "
				 "%-12s %-8s %-8s\n" % \
				 ("pool", "objset", "object", "type", "btype",
				  "data_bs", "meta_bs", "bsize", "lvls",
				  "holds", "blocks", "size", "cached",
				  "direct", "indirect", "bonus", "spill"))
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

	for pool in d.iterkeys():
		for objset in d[pool].iterkeys():
			for objnum in d[pool][objset].iterkeys():
				objects_print_dict_object(d, pool,
							  objset, objnum)

def types_update_dict(d, pool, objset, objnum, level, blkid, bufsize, objtype):
	if pool not in d:
		d[pool] = dict()

	if objset not in d[pool]:
		d[pool][objset] = dict()

	if objtype not in d[pool][objset]:
		d[pool][objset][objtype] = dict({'cached'  : 0,
						 'direct'  : 0,
						 'indirect': 0,
						 'bonus'   : 0,
						 'spill'   : 0})

	d[pool][objset][objtype]['cached'] += bufsize

	if blkid == -1:
		d[pool][objset][objtype]['bonus'] += bufsize
	elif blkid == -2:
		d[pool][objset][objtype]['spill'] += bufsize
	else:
		if level == 0:
			d[pool][objset][objtype]['direct'] += bufsize
		else:
			d[pool][objset][objtype]['indirect'] += bufsize

	return d

def types_build_dict(filehandle):
	d = dict()

	# First 3 lines are header information, skip these lines.
	for i in range(0, 3):
		next(filehandle)

	for line in filehandle:
		cols = line.split()

		types_update_dict(d, cols[0], int(cols[1]), int(cols[2]),
				  int(cols[3]), int(cols[4]), int(cols[6]),
				  int(cols[28]))

	return d

def types_print_dict_object(d, pool, objset, objtype):
	o = d[pool][objset][objtype]
	t = get_typestring(objtype)

	try:
		sys.stdout.write("%-16s %-8i %-28s | "
				 "%-12i %-12i %-12i %-8i %-8i\n" % \
				 (pool, objset, t, o['cached'],
				  o['direct'], o['indirect'], o['bonus'],
				  o['spill']))
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def types_print_dict(d):
	try:
		sys.stdout.write("%-16s %-8s %-28s | "
				 "%-12s %-12s %-12s %-8s %-8s\n" % \
				 ("pool", "objset", "type", "cached",
				  "direct", "indirect", "bonus", "spill"))
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

	for pool in d.iterkeys():
		for objset in d[pool].iterkeys():
			for objtype in d[pool][objset].iterkeys():
				types_print_dict_object(d, pool,
							objset, objtype)

def bufs_print_bufs(filehandle):
	try:
		# For an initial implementation, just print stdin verbatim
		for line in filehandle:
			sys.stdout.write(line);
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def main():
	hflag    = False
	values   = False
	extended = False
	buffers  = False
	objects  = False
	types    = False
	ifile    = None
	i        = 1

	try:
		opts, args = getopt.getopt(sys.argv[1:], "hbot",
					   ["help", "extended", "values",
					    "buffers", "objects", "types"])
	except getopt.error:
		usage()

	for opt, arg in opts:
		if opt in ('-h', '--help'):
			hflag = True
		if opt in ('-v', '--values'):
			values = True;
		if opt in ('-x', '--extended'):
			extended = True;
		if opt in ('-b', '--buffers'):
			buffers = True
		if opt in ('-o', '--objects'):
			objects = True
		if opt in ('-t', '--types'):
			types = True
		i += 1

	if hflag:
		usage()

	try:
		ifile = sys.argv[i]
	except IndexError:
		ifile = '/proc/spl/kstat/zfs/dbufs'

	if ifile is not "-":
		try:
			tmp = open(ifile, "r")
			sys.stdin = tmp
		except:
			sys.stderr.write("Cannot open %s for reading\n" % ifile)
			sys.exit(1)

	# default to "--buffers" if none set
	if not (buffers or objects or types):
		buffers = True

	if buffers:
		bufs_print_bufs(sys.stdin)

	if objects:
		objects_print_dict(objects_build_dict(sys.stdin))

	if types:
		types_print_dict(types_build_dict(sys.stdin))

if __name__ == '__main__':
	main()
