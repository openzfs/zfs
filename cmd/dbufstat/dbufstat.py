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

bhdr = ["pool", "objset", "object", "level", "blkid", "offset", "dbsize"]
bxhdr = ["pool", "objset", "object", "level", "blkid", "offset", "dbsize",
         "meta", "state", "dbholds", "list", "atype", "index", "flags",
         "count", "asize", "access", "mru", "gmru", "mfu", "gmfu", "l2",
         "l2_dattr", "l2_asize", "l2_comp", "aholds", "dtype", "btype",
         "data_bs", "meta_bs", "bsize", "lvls", "dholds", "blocks", "dsize"]
bincompat = ["cached", "direct", "indirect", "bonus", "spill"]

dhdr = ["pool", "objset", "object", "dtype", "cached"]
dxhdr = ["pool", "objset", "object", "dtype", "btype", "data_bs", "meta_bs",
         "bsize", "lvls", "dholds", "blocks", "dsize", "cached", "direct",
         "indirect", "bonus", "spill"]
dincompat = ["level", "blkid", "offset", "dbsize", "meta", "state", "dbholds",
             "list", "atype", "index", "flags", "count", "asize", "access",
             "mru", "gmru", "mfu", "gmfu", "l2", "l2_dattr", "l2_asize",
             "l2_comp", "aholds"]

thdr = ["pool", "objset", "dtype", "cached"]
txhdr = ["pool", "objset", "dtype", "cached", "direct", "indirect",
         "bonus", "spill"]
tincompat = ["object", "level", "blkid", "offset", "dbsize", "meta", "state",
             "dbholds", "list", "atype", "index", "flags", "count", "asize",
             "access", "mru", "gmru", "mfu", "gmfu", "l2", "l2_dattr",
             "l2_asize", "l2_comp", "aholds", "btype", "data_bs", "meta_bs",
             "bsize", "lvls", "dholds", "blocks", "dsize"]

cols = {
    # hdr:        [size, scale, description]
    "pool":       [15,   -1, "pool name"],
    "objset":     [6,    -1, "dataset identification number"],
    "object":     [10,   -1, "object number"],
    "level":      [5,    -1, "indirection level of buffer"],
    "blkid":      [8,    -1, "block number of buffer"],
    "offset":     [12, 1024, "offset in object of buffer"],
    "dbsize":     [7,  1024, "size of buffer"],
    "meta":       [4,    -1, "is this buffer metadata?"],
    "state":      [5,    -1, "state of buffer (read, cached, etc)"],
    "dbholds":    [7,  1000, "number of holds on buffer"],
    "list":       [4,    -1, "which ARC list contains this buffer"],
    "atype":      [7,    -1, "ARC header type (data or metadata)"],
    "index":      [5,    -1, "buffer's index into its ARC list"],
    "flags":      [8,    -1, "ARC read flags"],
    "count":      [5,    -1, "ARC data count"],
    "asize":      [7,  1024, "size of this ARC buffer"],
    "access":     [10,   -1, "time this ARC buffer was last accessed"],
    "mru":        [5,  1000, "hits while on the ARC's MRU list"],
    "gmru":       [5,  1000, "hits while on the ARC's MRU ghost list"],
    "mfu":        [5,  1000, "hits while on the ARC's MFU list"],
    "gmfu":       [5,  1000, "hits while on the ARC's MFU ghost list"],
    "l2":         [5,  1000, "hits while on the L2ARC"],
    "l2_dattr":   [8,    -1, "L2ARC disk address/offset"],
    "l2_asize":   [8,  1024, "L2ARC alloc'd size (depending on compression)"],
    "l2_comp":    [21,   -1, "L2ARC compression algorithm for buffer"],
    "aholds":     [6,  1000, "number of holds on this ARC buffer"],
    "dtype":      [27,   -1, "dnode type"],
    "btype":      [27,   -1, "bonus buffer type"],
    "data_bs":    [7,  1024, "data block size"],
    "meta_bs":    [7,  1024, "metadata block size"],
    "bsize":      [6,  1024, "bonus buffer size"],
    "lvls":       [6,    -1, "number of indirection levels"],
    "dholds":     [6,  1000, "number of holds on dnode"],
    "blocks":     [8,  1000, "number of allocated blocks"],
    "dsize":      [12, 1024, "size of dnode"],
    "cached":     [6,  1024, "bytes cached for all blocks"],
    "direct":     [6,  1024, "bytes cached for direct blocks"],
    "indirect":   [8,  1024, "bytes cached for indirect blocks"],
    "bonus":      [5,  1024, "bytes cached for bonus buffer"],
    "spill":      [5,  1024, "bytes cached for spill block"],
}

hdr = None
xhdr = None
sep = "  "  # Default separator is 2 spaces
cmd = ("Usage: dbufstat.py [-bdhrtvx] [-i file] [-f fields] [-o file] "
       "[-s string]\n")
raw = 0


def print_incompat_helper(incompat):
    cnt = 0
    for key in sorted(incompat):
        if cnt is 0:
            sys.stderr.write("\t")
        elif cnt > 8:
            sys.stderr.write(",\n\t")
            cnt = 0
        else:
            sys.stderr.write(", ")

        sys.stderr.write("%s" % key)
        cnt += 1

    sys.stderr.write("\n\n")


def detailed_usage():
    sys.stderr.write("%s\n" % cmd)

    sys.stderr.write("Field definitions incompatible with '-b' option:\n")
    print_incompat_helper(bincompat)

    sys.stderr.write("Field definitions incompatible with '-d' option:\n")
    print_incompat_helper(dincompat)

    sys.stderr.write("Field definitions incompatible with '-t' option:\n")
    print_incompat_helper(tincompat)

    sys.stderr.write("Field definitions are as follows:\n")
    for key in sorted(cols.keys()):
        sys.stderr.write("%11s : %s\n" % (key, cols[key][2]))
    sys.stderr.write("\n")

    sys.exit(1)


def usage():
    sys.stderr.write("%s\n" % cmd)
    sys.stderr.write("\t -b : Print table of information for each dbuf\n")
    sys.stderr.write("\t -d : Print table of information for each dnode\n")
    sys.stderr.write("\t -h : Print this help message\n")
    sys.stderr.write("\t -r : Print raw values\n")
    sys.stderr.write("\t -t : Print table of information for each dnode type"
                     "\n")
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
    sys.stderr.write("\tdbufstat.py -d -f pool,object,objset,dsize,cached\n")
    sys.stderr.write("\n")

    sys.exit(1)


def prettynum(sz, scale, num=0):
    global raw

    suffix = [' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z']
    index = 0
    save = 0

    if raw or scale == -1:
        return "%*s" % (sz, num)

    # Rounding error, return 0
    elif 0 < num < 1:
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


def print_values(v):
    global hdr
    global sep

    try:
        for col in hdr:
            sys.stdout.write("%s%s" % (
                prettynum(cols[col][0], cols[col][1], v[col]), sep))
        sys.stdout.write("\n")
    except IOError as e:
        if e.errno == errno.EPIPE:
            sys.exit(1)


def print_header():
    global hdr
    global sep

    try:
        for col in hdr:
            sys.stdout.write("%*s%s" % (cols[col][0], col, sep))
        sys.stdout.write("\n")
    except IOError as e:
        if e.errno == errno.EPIPE:
            sys.exit(1)


def get_typestring(t):
    type_strings = ["DMU_OT_NONE",
                    # general:
                    "DMU_OT_OBJECT_DIRECTORY",
                    "DMU_OT_OBJECT_ARRAY",
                    "DMU_OT_PACKED_NVLIST",
                    "DMU_OT_PACKED_NVLIST_SIZE",
                    "DMU_OT_BPOBJ",
                    "DMU_OT_BPOBJ_HDR",
                    # spa:
                    "DMU_OT_SPACE_MAP_HEADER",
                    "DMU_OT_SPACE_MAP",
                    # zil:
                    "DMU_OT_INTENT_LOG",
                    # dmu:
                    "DMU_OT_DNODE",
                    "DMU_OT_OBJSET",
                    # dsl:
                    "DMU_OT_DSL_DIR",
                    "DMU_OT_DSL_DIR_CHILD_MAP",
                    "DMU_OT_DSL_DS_SNAP_MAP",
                    "DMU_OT_DSL_PROPS",
                    "DMU_OT_DSL_DATASET",
                    # zpl:
                    "DMU_OT_ZNODE",
                    "DMU_OT_OLDACL",
                    "DMU_OT_PLAIN_FILE_CONTENTS",
                    "DMU_OT_DIRECTORY_CONTENTS",
                    "DMU_OT_MASTER_NODE",
                    "DMU_OT_UNLINKED_SET",
                    # zvol:
                    "DMU_OT_ZVOL",
                    "DMU_OT_ZVOL_PROP",
                    # other; for testing only!
                    "DMU_OT_PLAIN_OTHER",
                    "DMU_OT_UINT64_OTHER",
                    "DMU_OT_ZAP_OTHER",
                    # new object types:
                    "DMU_OT_ERROR_LOG",
                    "DMU_OT_SPA_HISTORY",
                    "DMU_OT_SPA_HISTORY_OFFSETS",
                    "DMU_OT_POOL_PROPS",
                    "DMU_OT_DSL_PERMS",
                    "DMU_OT_ACL",
                    "DMU_OT_SYSACL",
                    "DMU_OT_FUID",
                    "DMU_OT_FUID_SIZE",
                    "DMU_OT_NEXT_CLONES",
                    "DMU_OT_SCAN_QUEUE",
                    "DMU_OT_USERGROUP_USED",
                    "DMU_OT_USERGROUP_QUOTA",
                    "DMU_OT_USERREFS",
                    "DMU_OT_DDT_ZAP",
                    "DMU_OT_DDT_STATS",
                    "DMU_OT_SA",
                    "DMU_OT_SA_MASTER_NODE",
                    "DMU_OT_SA_ATTR_REGISTRATION",
                    "DMU_OT_SA_ATTR_LAYOUTS",
                    "DMU_OT_SCAN_XLATE",
                    "DMU_OT_DEDUP",
                    "DMU_OT_DEADLIST",
                    "DMU_OT_DEADLIST_HDR",
                    "DMU_OT_DSL_CLONES",
                    "DMU_OT_BPOBJ_SUBOBJ"]

    # If "-rr" option is used, don't convert to string representation
    if raw > 1:
        return "%i" % t

    try:
        return type_strings[t]
    except IndexError:
        return "%i" % t


def get_compstring(c):
    comp_strings = ["ZIO_COMPRESS_INHERIT", "ZIO_COMPRESS_ON",
                    "ZIO_COMPRESS_OFF",     "ZIO_COMPRESS_LZJB",
                    "ZIO_COMPRESS_EMPTY",   "ZIO_COMPRESS_GZIP_1",
                    "ZIO_COMPRESS_GZIP_2",  "ZIO_COMPRESS_GZIP_3",
                    "ZIO_COMPRESS_GZIP_4",  "ZIO_COMPRESS_GZIP_5",
                    "ZIO_COMPRESS_GZIP_6",  "ZIO_COMPRESS_GZIP_7",
                    "ZIO_COMPRESS_GZIP_8",  "ZIO_COMPRESS_GZIP_9",
                    "ZIO_COMPRESS_ZLE",     "ZIO_COMPRESS_LZ4",
                    "ZIO_COMPRESS_FUNCTION"]

    # If "-rr" option is used, don't convert to string representation
    if raw > 1:
        return "%i" % c

    try:
        return comp_strings[c]
    except IndexError:
        return "%i" % c


def parse_line(line, labels):
    global hdr

    new = dict()
    val = None
    for col in hdr:
        # These are "special" fields computed in the update_dict
        # function, prevent KeyError exception on labels[col] for these.
        if col not in ['bonus', 'cached', 'direct', 'indirect', 'spill']:
            val = line[labels[col]]

        if col in ['pool', 'flags']:
            new[col] = str(val)
        elif col in ['dtype', 'btype']:
            new[col] = get_typestring(int(val))
        elif col in ['l2_comp']:
            new[col] = get_compstring(int(val))
        else:
            new[col] = int(val)

    return new


def update_dict(d, k, line, labels):
    pool = line[labels['pool']]
    objset = line[labels['objset']]
    key = line[labels[k]]

    dbsize = int(line[labels['dbsize']])
    blkid = int(line[labels['blkid']])
    level = int(line[labels['level']])

    if pool not in d:
        d[pool] = dict()

    if objset not in d[pool]:
        d[pool][objset] = dict()

    if key not in d[pool][objset]:
        d[pool][objset][key] = parse_line(line, labels)
        d[pool][objset][key]['bonus'] = 0
        d[pool][objset][key]['cached'] = 0
        d[pool][objset][key]['direct'] = 0
        d[pool][objset][key]['indirect'] = 0
        d[pool][objset][key]['spill'] = 0

    d[pool][objset][key]['cached'] += dbsize

    if blkid == -1:
        d[pool][objset][key]['bonus'] += dbsize
    elif blkid == -2:
        d[pool][objset][key]['spill'] += dbsize
    else:
        if level == 0:
            d[pool][objset][key]['direct'] += dbsize
        else:
            d[pool][objset][key]['indirect'] += dbsize

    return d


def print_dict(d):
    print_header()
    for pool in d.keys():
        for objset in d[pool].keys():
            for v in d[pool][objset].values():
                print_values(v)


def dnodes_build_dict(filehandle):
    labels = dict()
    dnodes = dict()

    # First 3 lines are header information, skip the first two
    for i in range(2):
        next(filehandle)

    # The third line contains the labels and index locations
    for i, v in enumerate(next(filehandle).split()):
        labels[v] = i

    # The rest of the file is buffer information
    for line in filehandle:
        update_dict(dnodes, 'object', line.split(), labels)

    return dnodes


def types_build_dict(filehandle):
    labels = dict()
    types = dict()

    # First 3 lines are header information, skip the first two
    for i in range(2):
        next(filehandle)

    # The third line contains the labels and index locations
    for i, v in enumerate(next(filehandle).split()):
        labels[v] = i

    # The rest of the file is buffer information
    for line in filehandle:
        update_dict(types, 'dtype', line.split(), labels)

    return types


def buffers_print_all(filehandle):
    labels = dict()

    # First 3 lines are header information, skip the first two
    for i in range(2):
        next(filehandle)

    # The third line contains the labels and index locations
    for i, v in enumerate(next(filehandle).split()):
        labels[v] = i

    print_header()

    # The rest of the file is buffer information
    for line in filehandle:
        print_values(parse_line(line.split(), labels))


def main():
    global hdr
    global sep
    global raw

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
            "bdf:hi:o:rs:tvx",
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
        opts = None

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
        if opt in ('-r', '--raw'):
            raw += 1
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

    if bflag:
        hdr = bxhdr if xflag else bhdr
    elif tflag:
        hdr = txhdr if xflag else thdr
    else:  # Even if dflag is False, it's the default if none set
        dflag = True
        hdr = dxhdr if xflag else dhdr

    if desired_cols:
        hdr = desired_cols.split(",")

        invalid = []
        incompat = []
        for ele in hdr:
            if ele not in cols:
                invalid.append(ele)
            elif ((bflag and bincompat and ele in bincompat) or
                  (dflag and dincompat and ele in dincompat) or
                  (tflag and tincompat and ele in tincompat)):
                    incompat.append(ele)

        if len(invalid) > 0:
            sys.stderr.write("Invalid column definition! -- %s\n" % invalid)
            usage()

        if len(incompat) > 0:
            sys.stderr.write("Incompatible field specified! -- %s\n" %
                             incompat)
            usage()

    if ofile:
        try:
            tmp = open(ofile, "w")
            sys.stdout = tmp

        except IOError:
            sys.stderr.write("Cannot open %s for writing\n" % ofile)
            sys.exit(1)

    if not ifile:
        ifile = '/proc/spl/kstat/zfs/dbufs'

    if ifile is not "-":
        try:
            tmp = open(ifile, "r")
            sys.stdin = tmp
        except IOError:
            sys.stderr.write("Cannot open %s for reading\n" % ifile)
            sys.exit(1)

    if bflag:
        buffers_print_all(sys.stdin)

    if dflag:
        print_dict(dnodes_build_dict(sys.stdin))

    if tflag:
        print_dict(types_build_dict(sys.stdin))

if __name__ == '__main__':
    main()
