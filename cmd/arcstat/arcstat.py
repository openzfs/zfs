#!/usr/bin/python
#
# Print out ZFS ARC Statistics exported via kstat(1)
# For a definition of fields, or usage, use arctstat.pl -v
#
# This script is a fork of the original arcstat.pl (0.1) by
# Neelakanth Nadgir, originally published on his Sun blog on
# 09/18/2007
#     http://blogs.sun.com/realneel/entry/zfs_arc_statistics
#
# This version aims to improve upon the original by adding features
# and fixing bugs as needed.  This version is maintained by
# Mike Harsch and is hosted in a public open source repository:
#    http://github.com/mharsch/arcstat
#
# Comments, Questions, or Suggestions are always welcome.
# Contact the maintainer at ( mike at harschsystems dot com )
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
#
# Fields have a fixed width. Every interval, we fill the "v"
# hash with its corresponding value (v[field]=value) using calculate().
# @hdr is the array of fields that needs to be printed, so we
# just iterate over this array and print the values using our pretty printer.
#


import sys
import time
import getopt
import re
import copy

from decimal import Decimal
from signal import signal, SIGINT, SIGWINCH, SIG_DFL

cols = {
    # HDR:        [Size, Scale, Description]
    "time":       [8, -1, "Time"],
    "hits":       [4, 1000, "ARC reads per second"],
    "miss":       [4, 1000, "ARC misses per second"],
    "read":       [4, 1000, "Total ARC accesses per second"],
    "hit%":       [4, 100, "ARC Hit percentage"],
    "miss%":      [5, 100, "ARC miss percentage"],
    "dhit":       [4, 1000, "Demand hits per second"],
    "dmis":       [4, 1000, "Demand misses per second"],
    "dh%":        [3, 100, "Demand hit percentage"],
    "dm%":        [3, 100, "Demand miss percentage"],
    "phit":       [4, 1000, "Prefetch hits per second"],
    "pmis":       [4, 1000, "Prefetch misses per second"],
    "ph%":        [3, 100, "Prefetch hits percentage"],
    "pm%":        [3, 100, "Prefetch miss percentage"],
    "mhit":       [4, 1000, "Metadata hits per second"],
    "mmis":       [4, 1000, "Metadata misses per second"],
    "mread":      [4, 1000, "Metadata accesses per second"],
    "mh%":        [3, 100, "Metadata hit percentage"],
    "mm%":        [3, 100, "Metadata miss percentage"],
    "arcsz":      [5, 1024, "ARC Size"],
    "c":          [4, 1024, "ARC Target Size"],
    "mfu":        [4, 1000, "MFU List hits per second"],
    "mru":        [4, 1000, "MRU List hits per second"],
    "mfug":       [4, 1000, "MFU Ghost List hits per second"],
    "mrug":       [4, 1000, "MRU Ghost List hits per second"],
    "eskip":      [5, 1000, "evict_skip per second"],
    "mtxmis":     [6, 1000, "mutex_miss per second"],
    "rmis":       [4, 1000, "recycle_miss per second"],
    "dread":      [5, 1000, "Demand accesses per second"],
    "pread":      [5, 1000, "Prefetch accesses per second"],
    "l2hits":     [6, 1000, "L2ARC hits per second"],
    "l2miss":     [6, 1000, "L2ARC misses per second"],
    "l2read":     [6, 1000, "Total L2ARC accesses per second"],
    "l2hit%":     [6, 100, "L2ARC access hit percentage"],
    "l2miss%":    [7, 100, "L2ARC access miss percentage"],
    "l2asize":    [7, 1024, "Actual (compressed) size of the L2ARC"],
    "l2size":     [6, 1024, "Size of the L2ARC"],
    "l2bytes":    [7, 1024, "bytes read per second from the L2ARC"],
}

v = {}
hdr = ["time", "read", "miss", "miss%", "dmis", "dm%", "pmis", "pm%", "mmis",
       "mm%", "arcsz", "c"]
xhdr = ["time", "mfu", "mru", "mfug", "mrug", "eskip", "mtxmis", "rmis",
        "dread", "pread", "read"]
sint = 1               # Default interval is 1 second
count = 1              # Default count is 1
hdr_intr = 20          # Print header every 20 lines of output
opfile = None
sep = "  "              # Default separator is 2 spaces
version = "0.4"
l2exist = False
cmd = ("Usage: arcstat.py [-hvx] [-f fields] [-o file] [-s string] [interval "
       "[count]]\n")
cur = {}
d = {}
out = None
kstat = None
float_pobj = re.compile("^[0-9]+(\.[0-9]+)?$")


def detailed_usage():
    sys.stderr.write("%s\n" % cmd)
    sys.stderr.write("Field definitions are as follows:\n")
    for key in cols:
        sys.stderr.write("%11s : %s\n" % (key, cols[key][2]))
    sys.stderr.write("\n")

    sys.exit(1)


def usage():
    sys.stderr.write("%s\n" % cmd)
    sys.stderr.write("\t -h : Print this help message\n")
    sys.stderr.write("\t -v : List all possible field headers and definitions"
                     "\n")
    sys.stderr.write("\t -x : Print extended stats\n")
    sys.stderr.write("\t -f : Specify specific fields to print (see -v)\n")
    sys.stderr.write("\t -o : Redirect output to the specified file\n")
    sys.stderr.write("\t -s : Override default field separator with custom "
                     "character or string\n")
    sys.stderr.write("\nExamples:\n")
    sys.stderr.write("\tarcstat.py -o /tmp/a.log 2 10\n")
    sys.stderr.write("\tarcstat.py -s \",\" -o /tmp/a.log 2 10\n")
    sys.stderr.write("\tarcstat.py -v\n")
    sys.stderr.write("\tarcstat.py -f time,hit%,dh%,ph%,mh% 1\n")
    sys.stderr.write("\n")

    sys.exit(1)


def kstat_update():
    global kstat

    k = [line.strip() for line in open('/proc/spl/kstat/zfs/arcstats')]

    if not k:
        sys.exit(1)

    del k[0:2]
    kstat = {}

    for s in k:
        if not s:
            continue

        name, unused, value = s.split()
        kstat[name] = Decimal(value)


def snap_stats():
    global cur
    global kstat

    prev = copy.deepcopy(cur)
    kstat_update()

    cur = kstat
    for key in cur:
        if re.match(key, "class"):
            continue
        if key in prev:
            d[key] = cur[key] - prev[key]
        else:
            d[key] = cur[key]


def prettynum(sz, scale, num=0):
    suffix = [' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z']
    index = 0
    save = 0

    # Special case for date field
    if scale == -1:
        return "%s" % num

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


def print_values():
    global hdr
    global sep
    global v

    for col in hdr:
        sys.stdout.write("%s%s" % (
            prettynum(cols[col][0], cols[col][1], v[col]),
            sep
        ))
    sys.stdout.write("\n")


def print_header():
    global hdr
    global sep

    for col in hdr:
        sys.stdout.write("%*s%s" % (cols[col][0], col, sep))
    sys.stdout.write("\n")

def get_terminal_lines():
    try:
        import fcntl, termios, struct
        data = fcntl.ioctl(sys.stdout.fileno(), termios.TIOCGWINSZ, '1234')
        sz = struct.unpack('hh', data)
        return sz[0]
    except:
        pass

def update_hdr_intr():
    global hdr_intr

    lines = get_terminal_lines()
    if lines and lines > 3:
        hdr_intr = lines - 3

def resize_handler(signum, frame):
    update_hdr_intr()


def init():
    global sint
    global count
    global hdr
    global xhdr
    global opfile
    global sep
    global out
    global l2exist

    desired_cols = None
    xflag = False
    hflag = False
    vflag = False
    i = 1

    try:
        opts, args = getopt.getopt(
            sys.argv[1:],
            "xo:hvs:f:",
            [
                "extended",
                "outfile",
                "help",
                "verbose",
                "seperator",
                "columns"
            ]
        )
    except getopt.error as msg:
        sys.stderr.write(msg)
        usage()
        opts = None

    for opt, arg in opts:
        if opt in ('-x', '--extended'):
            xflag = True
        if opt in ('-o', '--outfile'):
            opfile = arg
            i += 1
        if opt in ('-h', '--help'):
            hflag = True
        if opt in ('-v', '--verbose'):
            vflag = True
        if opt in ('-s', '--seperator'):
            sep = arg
            i += 1
        if opt in ('-f', '--columns'):
            desired_cols = arg
            i += 1
        i += 1

    argv = sys.argv[i:]
    sint = Decimal(argv[0]) if argv else sint
    count = int(argv[1]) if len(argv) > 1 else count

    if len(argv) > 1:
        sint = Decimal(argv[0])
        count = int(argv[1])

    elif len(argv) > 0:
        sint = Decimal(argv[0])
        count = 0

    if hflag or (xflag and desired_cols):
        usage()

    if vflag:
        detailed_usage()

    if xflag:
        hdr = xhdr

    update_hdr_intr()

    # check if L2ARC exists
    snap_stats()
    l2_size = cur.get("l2_size")
    if l2_size:
        l2exist = True

    if desired_cols:
        hdr = desired_cols.split(",")

        invalid = []
        incompat = []
        for ele in hdr:
            if ele not in cols:
                invalid.append(ele)
            elif not l2exist and ele.startswith("l2"):
                sys.stdout.write("No L2ARC Here\n%s\n" % ele)
                incompat.append(ele)

        if len(invalid) > 0:
            sys.stderr.write("Invalid column definition! -- %s\n" % invalid)
            usage()

        if len(incompat) > 0:
            sys.stderr.write("Incompatible field specified! -- %s\n" %
                             incompat)
            usage()

    if opfile:
        try:
            out = open(opfile, "w")
            sys.stdout = out

        except IOError:
            sys.stderr.write("Cannot open %s for writing\n" % opfile)
            sys.exit(1)


def calculate():
    global d
    global v
    global l2exist

    v = dict()
    v["time"] = time.strftime("%H:%M:%S", time.localtime())
    v["hits"] = d["hits"] / sint
    v["miss"] = d["misses"] / sint
    v["read"] = v["hits"] + v["miss"]
    v["hit%"] = 100 * v["hits"] / v["read"] if v["read"] > 0 else 0
    v["miss%"] = 100 - v["hit%"] if v["read"] > 0 else 0

    v["dhit"] = (d["demand_data_hits"] + d["demand_metadata_hits"]) / sint
    v["dmis"] = (d["demand_data_misses"] + d["demand_metadata_misses"]) / sint

    v["dread"] = v["dhit"] + v["dmis"]
    v["dh%"] = 100 * v["dhit"] / v["dread"] if v["dread"] > 0 else 0
    v["dm%"] = 100 - v["dh%"] if v["dread"] > 0 else 0

    v["phit"] = (d["prefetch_data_hits"] + d["prefetch_metadata_hits"]) / sint
    v["pmis"] = (d["prefetch_data_misses"] +
                 d["prefetch_metadata_misses"]) / sint

    v["pread"] = v["phit"] + v["pmis"]
    v["ph%"] = 100 * v["phit"] / v["pread"] if v["pread"] > 0 else 0
    v["pm%"] = 100 - v["ph%"] if v["pread"] > 0 else 0

    v["mhit"] = (d["prefetch_metadata_hits"] +
                 d["demand_metadata_hits"]) / sint
    v["mmis"] = (d["prefetch_metadata_misses"] +
                 d["demand_metadata_misses"]) / sint

    v["mread"] = v["mhit"] + v["mmis"]
    v["mh%"] = 100 * v["mhit"] / v["mread"] if v["mread"] > 0 else 0
    v["mm%"] = 100 - v["mh%"] if v["mread"] > 0 else 0

    v["arcsz"] = cur["size"]
    v["c"] = cur["c"]
    v["mfu"] = d["mfu_hits"] / sint
    v["mru"] = d["mru_hits"] / sint
    v["mrug"] = d["mru_ghost_hits"] / sint
    v["mfug"] = d["mfu_ghost_hits"] / sint
    v["eskip"] = d["evict_skip"] / sint
    v["rmis"] = d["recycle_miss"] / sint
    v["mtxmis"] = d["mutex_miss"] / sint

    if l2exist:
        v["l2hits"] = d["l2_hits"] / sint
        v["l2miss"] = d["l2_misses"] / sint
        v["l2read"] = v["l2hits"] + v["l2miss"]
        v["l2hit%"] = 100 * v["l2hits"] / v["l2read"] if v["l2read"] > 0 else 0

        v["l2miss%"] = 100 - v["l2hit%"] if v["l2read"] > 0 else 0
        v["l2asize"] = cur["l2_asize"]
        v["l2size"] = cur["l2_size"]
        v["l2bytes"] = d["l2_read_bytes"] / sint


def main():
    global sint
    global count
    global hdr_intr

    i = 0
    count_flag = 0

    init()
    if count > 0:
        count_flag = 1

    signal(SIGINT, SIG_DFL)
    signal(SIGWINCH, resize_handler)
    while True:
        if i == 0:
            print_header()

        snap_stats()
        calculate()
        print_values()

        if count_flag == 1:
            if count <= 1:
                break
            count -= 1

        i = 0 if i >= hdr_intr else i + 1
        time.sleep(sint)

    if out:
        out.close()


if __name__ == '__main__':
    main()
