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

dhdr  = ["pool", "objset", "object", "type", "size", "cached"]
dxhdr = ["pool", "objset", "object", "type", "btype", "data_bs", "meta_bs",
    "bsize", "lvls", "holds", "blocks", "size", "cached", "direct",
    "indirect", "bonus", "spill"]
dcols = {
    # hdr:        [size, scale, description]
    "pool":       [15,   -1, "pool name"],
    "objset":     [6,  1000, "dataset identification number"],
    "object":     [6,  1000, "object number"],
    "type":       [27,   -1, "object type"],
    "btype":      [27,   -1, "bonus buffer type"],
    "data_bs":    [7,  1024, "data block size"],
    "meta_bs":    [7,  1024, "metadata block size"],
    "bsize":      [6,  1024, "bonus buffer size"],
    "lvls":       [6,  1000, "number of indirection levels"],
    "holds":      [5,  1000, "number of holds on dnode"],
    "blocks":     [6,  1000, "number of allocated blocks"],
    "size":       [5,  1024, "size of dnode"],
    "cached":     [6,  1024, "bytes cached for all blocks"],
    "direct":     [6,  1024, "bytes cached for direct blocks"],
    "indirect":   [8,  1024, "bytes cached for indirect blocks"],
    "bonus":      [5,  1024, "bytes cached for bonus buffer"],
    "spill":      [5,  1024, "bytes cached for spill block"],
}

thdr  = ["pool", "objset", "type", "cached"]
txhdr = ["pool", "objset", "type", "cached", "direct", "indirect",
    "bonus", "spill"]
tcols = {
    # hdr:        [size, scale, description]
    "pool":       [15,   -1, "pool name"],
    "objset":     [6,  1000, "dataset identification number"],
    "type":       [27,   -1, "object type"],
    "cached":     [6,  1024, "bytes cached for all blocks"],
    "direct":     [6,  1024, "bytes cached for direct blocks"],
    "indirect":   [8,  1024, "bytes cached for indirect blocks"],
    "bonus":      [5,  1024, "bytes cached for bonus buffer"],
    "spill":      [5,  1024, "bytes cached for spill block"],
}

cols = None
hdr  = None
xhdr = None
sep = "  " # Default separator is 2 spaces
cmd = ("Usage: dbufstat.py [-bdhtvx] [-i file] [-f fields] [-o file] "
	"[-s string]\n")

def detailed_usage():
	sys.stderr.write("%s\n" % cmd)
	# TODO: Implement the '--buffers' option
#	sys.stderr.write("Field definitions are as follows when using '-b':\n")
#	for key in sorted(bcols.keys()):
#		sys.stderr.write("%11s : %s\n" % (key, bcols[key][2]))
#	sys.stderr.write("\n")

	sys.stderr.write("Field definitions are as follows when using '-d':\n")
	for key in sorted(dcols.keys()):
		sys.stderr.write("%11s : %s\n" % (key, dcols[key][2]))
	sys.stderr.write("\n")

	sys.stderr.write("Field definitions are as follows when using '-t':\n")
	for key in sorted(tcols.keys()):
		sys.stderr.write("%11s : %s\n" % (key, tcols[key][2]))
	sys.stderr.write("\n")

	sys.exit(1)

def usage():
	sys.stderr.write("%s\n" % cmd)
	sys.stderr.write("\t -b : Print table of information for each dbuf\n")
	sys.stderr.write("\t -d : Print table of information for each dnode\n")
	sys.stderr.write("\t -h : Print this help message\n")
	sys.stderr.write("\t -t : Print table of information for each dnode type\n")
	sys.stderr.write("\t -v : List all possible field headers and definitions"
        "\n")
	sys.stderr.write("\t -x : Print extended stats\n")
	sys.stderr.write("\t -i : Redirect input from the specified file\n")
	sys.stderr.write("\t -f : Specify specific fields to print (see -v)\n")
	sys.stderr.write("\t -o : Redirect output to the specified file\n")
	sys.stderr.write("\t -s : Override default field separator with custom "
        "character or string\n")
	sys.stderr.write("\nExamples:\n")
	sys.stderr.write("\tdbufstat.py -d -o /tmp/d.log\n")
	sys.stderr.write("\tdbufstat.py -t -s \",\" -o /tmp/t.log\n")
	sys.stderr.write("\tdbufstat.py -v\n")
	sys.stderr.write("\tdbufstat.py -d -f pool,object,objset,size,cached\n")
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
		d[pool][objset][objnum] = dict({
						'pool'     : pool,
						'objset'   : objset,
						'object'   : objnum,
						'type'     : get_typestring(objtype),
						'btype'    : get_typestring(btype),
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
						'spill'    : 0
		})

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
		tmp = line.split()

		objects_update_dict(d, tmp[0], int(tmp[1]), int(tmp[2]),
				    int(tmp[3]), int(tmp[4]), int(tmp[6]),
				    int(tmp[28]), int(tmp[29]), int(tmp[30]),
				    int(tmp[31]), int(tmp[32]), int(tmp[33]),
				    int(tmp[34]), int(tmp[35]), int(tmp[36]))

	return d

def prettynum(sz, scale, num=0):
    suffix = [' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z']
    index = 0
    save = 0

    # Special case for date field
    if scale == -1:
        return "%*s" % (sz, num)

    # Rounding error, return 0
    elif num > 0 and num < 1:
        num = 0

    while num > scale and index < 5:
        save = num
        num = num / scale
        index += 1

    if index == 0:
        return "%*d" % (sz, num)

    if (save / scale) < 10:
        return "%*.1f%s" % (sz - 1, num, suffix[index])
    else:
        return "%*d%s" % (sz - 1, num, suffix[index])

def objects_print_dict_object(d, pool, objset, objnum):
	global hdr
	global sep

	o = d[pool][objset][objnum]
	try:
		for col in hdr:
			sys.stdout.write("%s%s" % (
				prettynum(cols[col][0], cols[col][1], o[col]), sep))
		sys.stdout.write("\n")
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def objects_print_dict(d):
	global hdr
	global sep

	try:
		for col in hdr:
			sys.stdout.write("%*s%s" % (cols[col][0], col, sep))
		sys.stdout.write("\n")
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

	for pool in d.iterkeys():
		for objset in d[pool].iterkeys():
			for objnum in d[pool][objset].iterkeys():
				objects_print_dict_object(d, pool, objset, objnum)

def types_update_dict(d, pool, objset, objnum, level, blkid, bufsize, objtype):
	if pool not in d:
		d[pool] = dict()

	if objset not in d[pool]:
		d[pool][objset] = dict()

	if objtype not in d[pool][objset]:
		d[pool][objset][objtype] = dict({
						'pool'    : pool,
						'objset'  : objset,
						'type'    : get_typestring(objtype),
						'cached'  : 0,
						'direct'  : 0,
						'indirect': 0,
						'bonus'   : 0,
						'spill'   : 0
		})

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
		tmp = line.split()

		types_update_dict(d, tmp[0], int(tmp[1]), int(tmp[2]),
				  int(tmp[3]), int(tmp[4]), int(tmp[6]),
				  int(tmp[28]))

	return d

def types_print_dict_object(d, pool, objset, objtype):
	global hdr
	global cols
	global sep

	o = d[pool][objset][objtype]
	try:
		for col in hdr:
			sys.stdout.write("%s%s" % (
				prettynum(cols[col][0], cols[col][1], o[col]), sep))
		sys.stdout.write("\n")
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def types_print_dict(d):
	global hdr
	global cols
	global sep

	try:
		for col in hdr:
			sys.stdout.write("%*s%s" % (cols[col][0], col, sep))
		sys.stdout.write("\n")
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
		# TODO: Implement the "--buffers" option
		# For an initial implementation, just print stdin verbatim
		for line in filehandle:
			sys.stdout.write(line);
	except IOError as e:
		if e.errno == errno.EPIPE:
			sys.exit(1)

def main():
	global hdr
	global cols
	global sep

	desired_cols = None
	bflag = False
	dflag = False
	hflag = False
	ifile = None
	ofile = None
	tflag = False
	vflag = False
	xflag = False

	try:
		opts, args = getopt.getopt(
			sys.argv[1:],
			"bdf:hi:o:s:tvx",
			[
				"buffers",
				"dnodes",
				"columns",
				"help",
				"infile",
				"outfile",
				"seperator",
				"types",
				"verbose",
				"extended"
			]
		)
	except getopt.error:
		usage()

	for opt, arg in opts:
		if opt in ('-b', '--buffers'):
			bflag = True
		if opt in ('-d', '--dnodes'):
			dflag = True
		if opt in ('-f', '--columns'):
			desired_cols = arg
		if opt in ('-h', '--help'):
			hflag = True
		if opt in ('-i', '--infile'):
			ifile = arg
		if opt in ('-o', '--outfile'):
			ofile = arg
		if opt in ('-s', '--seperator'):
			sep = arg
		if opt in ('-t', '--types'):
			tflag = True
		if opt in ('-v', '--verbose'):
			vflag = True
		if opt in ('-x', '--extended'):
			xflag = True

	if hflag or (xflag and desired_cols):
		usage()

	if vflag:
		detailed_usage()

	# Ensure at most only one of b, d, or t flags are set
	if (bflag and dflag) or (bflag and tflag) or (dflag and tflag):
		usage()

	if dflag:
		hdr  = dxhdr if xflag else dhdr
		cols = dcols
	elif tflag:
		hdr  = txhdr if xflag else thdr
		cols = tcols
	else: # Even if bflag is False, it's the default if none set
		# TODO: Implement the "--buffers" option
		#bflag = True
		#hdr = bxhdr if xflag else bhdr
		#cols = bcols
		# TODO: Until "--buffers" is implemented, default to '-d'
		bflag = False
		dflag = True
		hdr  = dxhdr if xflag else dhdr
		cols = dcols

	if desired_cols:
		hdr = desired_cols.split(",")

		invalid = []
		for ele in hdr:
			if ele not in cols:
				invalid.append(ele)

		if len(invalid) > 0:
			sys.stderr.write("Invalid column definition! -- %s\n" % invalid)
			usage()

	if ofile:
		try:
			tmp = open(ofile, "w")
			sys.stdout = tmp

		except:
			sys.stderr.write("Cannot open %s for writing\n", ofile)
			sys.exit(1)

	if not ifile:
		ifile = '/proc/spl/kstat/zfs/dbufs'

	if ifile is not "-":
		try:
			tmp = open(ifile, "r")
			sys.stdin = tmp
		except:
			sys.stderr.write("Cannot open %s for reading\n" % ifile)
			sys.exit(1)

	if bflag:
		bufs_print_bufs(sys.stdin)

	if dflag:
		objects_print_dict(objects_build_dict(sys.stdin))

	if tflag:
		types_print_dict(types_build_dict(sys.stdin))

if __name__ == '__main__':
	main()
