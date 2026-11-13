#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Every object linked into a directory records the ZAP cookie of its
# entry (SA_ZPL_PARENT_COOKIE), so that its name can be found without
# searching the directory: when a directory opened by file handle is
# reconnected (Linux zpl_get_name(), FreeBSD zfs_vptocnp()) and by
# zfs_obj_to_path() (zdb, zpool status -v).  The cookie must name the
# object's own entry after creates, renames (within a directory, to another
# directory, over an existing target, RENAME_EXCHANGE on Linux), links and
# send/receive; the kernel must find names by the cookie, without
# searching; and a stale cookie (its entry removed, reused by another
# object, or left behind by a rename that didn't update it, as an older
# implementation does) or a missing one (zfs_parent_cookie=0, objects from
# a version 4 file system) must fall back to searching the directory.
# The cookie is only a hint, so it must never be the reason an object
# needs a spill block: it is left out when the other attributes fit in
# the bonus buffer only without it.
#
# STRATEGY:
# 1. On a case-sensitive and a case-insensitive dataset, in a microzap and
#    a fat ZAP directory, create files and directories, rename, link and
#    unlink them, keeping the expected path and cookie state of each.
# 2. For each object, check zdb's path and cookie state: "valid" (the entry
#    at the cookie is the object's, and agrees with a lookup by name and a
#    search) for every object linked by this module, "stale" only for the
#    deliberately stale cases, "none" only for objects created with
#    zfs_parent_cookie=0.
# 3. Open each directory by file handle after remounting the dataset,
#    check the path the kernel reports (Linux: /proc/self/fd; FreeBSD:
#    getcwd() after fchdir()), and check the dirent_name kstat: the name
#    of the directory and of each ancestor is found once, by cookie if its
#    cookie is valid (no directory search), and by a search, counted as
#    stale or missing, otherwise.
# 4. With zfs_parent_cookie=0, rename directories within and between
#    directories (stale cookies on directories), reuse one's entry, then
#    rename one again with zfs_parent_cookie=1 (valid again), and repeat
#    the checks.
# 5. Send the case-sensitive dataset (full, then incremental after more
#    renames) and repeat the checks on the received one.
# 6. Create directories on a version 4 file system (no SA, no cookie),
#    upgrade it, add and rename more, and check that old objects have no
#    cookie until converted to SA and renamed, and new ones have valid
#    cookies.
# 7. With xattr=sa and dnodesize=1k, give files xattrs 8 bytes larger
#    each time, and check that the cookie stays until the attributes no
#    longer fit with it, is left out for the one size that fits only
#    without it, and that only larger xattrs use a spill block.
#

verify_runnable "global"

function cleanup
{
	typeset fs
	restore_tunable PARENT_COOKIE
	for fs in cs ci recv v4 spill; do
		datasetexists $TESTPOOL/$fs && destroy_dataset $TESTPOOL/$fs -r
	done
	rm -rf $TESTDIR1 $CHECK $TEST_BASE_DIR/exportfs_parent_cookie.*.json \
	    $TEST_BASE_DIR/exportfs_parent_cookie.zdb
}

log_assert "Parent cookies name their entries and are used, stale ones fall back"
log_onexit cleanup

CHECK=$TEST_BASE_DIR/exportfs_parent_cookie.py
log_must save_tunable PARENT_COOKIE

#
# python3 $CHECK MODE DATASET MOUNTPOINT EXPECT
#   make     build the tree; write each object's expected path and cookie
#            state to EXPECT
#   stale    with zfs_parent_cookie=0: rename directories without updating
#            their cookies, create objects without one
#   reuse    with zfs_parent_cookie=1: reuse a stale cookie's entry and
#            rename a directory with a stale cookie again
#   rename   more renames (for an incremental send)
#   v4       expectations of the version 4 file system
#   check    check zdb's path and cookie state of each object, and the path
#            and dirent_name kstat of each directory opened by handle
#
cat > $CHECK <<'PY'
import ctypes, ctypes.util, json, os, re, subprocess, sys

FREEBSD = os.uname().sysname == "FreeBSD"
libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
mode, ds, mnt, expf = sys.argv[1:5]
NBIG = 5000

def err(what):
    return "%s: %s" % (what, os.strerror(ctypes.get_errno()))

def load():
    return json.load(open(expf))

def save(exp):
    json.dump(exp, open(expf, "w"), indent=1, sort_keys=True)

# exp: relative path -> [kind, cookie state, path zdb reports for the object
# (None: another link's), and for a cookie whose entry was reused, the
# object that has that entry now]
def mk(exp, rel, kind, state="valid"):
    p = mnt + "/" + rel
    if kind == "d":
        os.mkdir(p)
    else:
        os.close(os.open(p, os.O_CREAT | os.O_WRONLY, 0o644))
    exp[rel] = [kind, state, rel]

def mv(exp, a, b, state="valid"):
    os.rename(mnt + "/" + a, mnt + "/" + b)
    exp.pop(b, None)
    exp[b] = [exp.pop(a)[0], state, b]

def link(exp, a, b):
    os.link(mnt + "/" + a, mnt + "/" + b)
    # the parent and the cookie follow the newest link
    exp[b] = [exp[a][0], "valid", b]
    exp[a] = [exp[a][0], "valid", None]

def exchange(exp, a, b):
    if libc.renameat2(-100, (mnt + "/" + a).encode(), -100,
                      (mnt + "/" + b).encode(), 2) != 0:
        sys.exit(err("renameat2 RENAME_EXCHANGE %s %s" % (a, b)))
    exp[a], exp[b] = [exp[b][0], "valid", a], [exp[a][0], "valid", b]

def make():
    exp = {}
    mk(exp, "other", "d")
    for d, n in (("small", 10), ("big", NBIG)):
        mk(exp, d, "d")
        for i in range(n):
            mk(exp, "%s/Dir%d" % (d, i), "d")
        for i in range(8):
            mk(exp, "%s/File%d" % (d, i), "f")
        # renamed within the directory, to another one, over a target
        mv(exp, "%s/Dir1" % d, "%s/Renamed" % d)
        mv(exp, "%s/File1" % d, "%s/FileRenamed" % d)
        mv(exp, "%s/Dir2" % d, "other/Moved-%s" % d)
        mv(exp, "%s/File2" % d, "other/FileMoved-%s" % d)
        mk(exp, "%s/Target" % d, "d")
        mv(exp, "%s/Dir3" % d, "%s/Target" % d)
        if not FREEBSD:
            mk(exp, "other/X-%s" % d, "d")
            exchange(exp, "%s/Dir4" % d, "other/X-%s" % d)
        # hard links in another directory and in the same one
        link(exp, "%s/File3" % d, "other/Link-%s" % d)
        link(exp, "%s/File7" % d, "%s/LinkA7" % d)
        link(exp, "%s/LinkA7" % d, "%s/LinkB7" % d)
        # stale: the cookie's entry removed; the old name is found by search
        link(exp, "%s/File4" % d, "%s/Gone4" % d)
        os.unlink("%s/%s/Gone4" % (mnt, d))
        exp.pop("%s/Gone4" % d)
        exp["%s/File4" % d] = ["f", "stale", "%s/File4" % d]
        # stale: the cookie's entry reused by another object
        link(exp, "%s/File5" % d, "%s/Reused5" % d)
        os.unlink("%s/%s/Reused5" % (mnt, d))
        exp["%s/File5" % d] = ["f", "stale", "%s/File5" % d,
                               "%s/Reused5" % d]
        mk(exp, "%s/Reused5" % d, "f")
        os.rmdir("%s/%s/Dir0" % (mnt, d))
        exp.pop("%s/Dir0" % d)
    save(exp)

def stale():
    exp = load()
    for d in ("small", "big"):
        # renames that leave the directory's cookie behind: its entry is
        # removed, points into another directory, or (reuse) is reused
        mv(exp, "%s/Dir5" % d, "%s/StaleRenamed" % d, "stale")
        mv(exp, "%s/Dir6" % d, "other/StaleMoved-%s" % d, "stale")
        mv(exp, "%s/Dir7" % d, "%s/StaleReused" % d, "stale")
        mv(exp, "%s/Dir8" % d, "%s/StaleAgain" % d, "stale")
        # created without a cookie
        mk(exp, "%s/Unrecorded" % d, "d", "none")
        mk(exp, "%s/UnrecordedFile" % d, "f", "none")
    save(exp)

def reuse():
    exp = load()
    for d in ("small", "big"):
        mk(exp, "%s/Dir7" % d, "d")
        exp["%s/StaleReused" % d].append("%s/Dir7" % d)
        mv(exp, "%s/StaleAgain" % d, "%s/Refreshed" % d)
    save(exp)

def rename():
    exp = load()
    for d in ("small", "big"):
        mv(exp, "%s/Dir9" % d, "%s/Renamed2" % d)
        mv(exp, "%s/File6" % d, "other/Moved2-%s" % d)
    save(exp)

def v4():
    exp = {"old": ["d", "absent", "old"]}
    for n in os.listdir(mnt + "/old"):
        exp["old/" + n] = ["d", "valid" if n.startswith("New") or
                           n == "Dir8r" else "absent", "old/" + n]
    save(exp)

def sampled(rel):
    # every entry of the small directories, a sample of the big ones
    m = re.fullmatch(r"(big|old)/(Dir|New)(\d+)", rel)
    return m is None or int(m.group(3)) % 500 in (3, 4, 7)

def zdb(objs):
    # path and cookie lines of each object, from one zdb run per batch
    res = {}
    objs = sorted(set(objs))
    for i in range(0, len(objs), 200):
        out = subprocess.run(["zdb", "-ddddd", ds] +
                             [str(o) for o in objs[i:i + 200]],
                             stdout=subprocess.PIPE,
                             universal_newlines=True).stdout
        obj, hdr = None, False
        for l in out.splitlines():
            w = l.split()
            if hdr and w and w[0].isdigit():
                obj = int(w[0])
                res[obj] = {}
            hdr = len(w) > 1 and w[0] == "Object" and w[1] == "lvl"
            if obj is not None and l.startswith("\t") and len(w) > 1:
                res[obj].setdefault(w[0], " ".join(w[1:]))
    return res

def kstats():
    names = ("cookie_hits", "cookie_stale", "cookie_missing", "searches")
    if FREEBSD:
        out = subprocess.run(["sysctl", "-n"] +
                             ["kstat.zfs.misc.dirent_name." + n for n in names],
                             stdout=subprocess.PIPE,
                             universal_newlines=True).stdout.split()
        if len(out) != len(names):
            sys.exit("FAIL: no kstat.zfs.misc.dirent_name")
        return dict(zip(names, map(int, out)))
    try:
        lines = open("/proc/spl/kstat/zfs/dirent_name").read().splitlines()
    except OSError as e:
        sys.exit("FAIL: /proc/spl/kstat/zfs/dirent_name: %s" % e)
    k = {w[0]: int(w[2]) for w in (l.split() for l in lines[2:])}
    return {n: k[n] for n in names}

class LinuxFH(ctypes.Structure):
    _fields_ = [("handle_bytes", ctypes.c_uint), ("handle_type", ctypes.c_int),
                ("f_handle", ctypes.c_ubyte * 128)]

class FreeBSDFH(ctypes.Structure):	# fhandle_t: fsid_t, struct fid
    _fields_ = [("fsid", ctypes.c_int32 * 2), ("fid_len", ctypes.c_ushort),
                ("fid_data0", ctypes.c_ushort), ("fid_data", ctypes.c_char * 16)]

def handle(p):
    if FREEBSD:
        fh = FreeBSDFH()
        if libc.getfh(p.encode(), ctypes.byref(fh)) != 0:
            sys.exit(err("getfh " + p))
        return fh
    fh, mid = LinuxFH(handle_bytes=128), ctypes.c_int()
    if libc.name_to_handle_at(-100, p.encode(), ctypes.byref(fh),
                              ctypes.byref(mid), 0) != 0:
        sys.exit(err("name_to_handle_at " + p))
    return fh

def remount():
    # forget every dentry, vnode and name cache entry of the dataset
    for c in ("unmount", "mount"):
        subprocess.run(["zfs", c, ds], check=True)

def path_by_handle(fh):
    # the path the kernel reconnects or rebuilds for a directory by handle
    if FREEBSD:
        fd = libc.fhopen(ctypes.byref(fh), os.O_RDONLY)
        if fd < 0:
            return err("fhopen")
        os.fchdir(fd)
        os.close(fd)
        got = os.getcwd()
        os.chdir("/")
        return got
    mfd = os.open(mnt, os.O_RDONLY | os.O_DIRECTORY)
    fd = libc.open_by_handle_at(mfd, ctypes.byref(fh), os.O_RDONLY)
    os.close(mfd)
    if fd < 0:
        return err("open_by_handle_at")
    got = os.readlink("/proc/self/fd/%d" % fd)
    os.close(fd)
    return got

def check():
    exp = {k: v for k, v in load().items() if sampled(k)}
    fails = 0
    subprocess.run(["zpool", "sync"], check=True)
    ino = {rel: os.lstat(mnt + "/" + rel).st_ino for rel in exp}
    z = zdb(ino.values())
    for rel, (kind, state, path, *reused) in sorted(exp.items()):
        if path is None:
            continue
        f = z.get(ino[rel], {})
        got = f.get("cookie", "-")
        gotstate = "absent" if got == "-" else got.split()[-1]
        msg = "zdb %s (%d): path %s, cookie %s" % (rel, ino[rel],
                                                   f.get("path"), got)
        if f.get("path") != "/" + path:
            print("FAIL: %s, expected path /%s" % (msg, path))
            fails += 1
        elif gotstate != state:
            print("FAIL: %s, expected %s" % (msg, state))
            fails += 1
        elif reused and z.get(ino[reused[0]], {}).get("cookie") != \
                got.replace("stale", "valid"):
            print("FAIL: %s, expected the cookie of %s (%s)" % (msg,
                  reused[0], z.get(ino[reused[0]], {}).get("cookie")))
            fails += 1
        else:
            print("ok: " + msg)
    # directories by handle, one at a time after a remount
    total = dict.fromkeys(kstats(), 0)
    for rel in sorted(r for r, v in exp.items() if v[0] == "d"):
        fh = handle(mnt + "/" + rel)
        remount()
        k0 = kstats()
        got = path_by_handle(fh)
        k1 = kstats()
        k = {n: k1[n] - k0[n] for n in k0}
        for n in k:
            total[n] += k[n]
        # After the remount the name of the directory and of each of its
        # ancestors below the root is looked up once: by cookie if valid,
        # by a search if stale or missing.
        parts = rel.split("/")
        states = [exp["/".join(parts[:i])][1]
                  for i in range(1, len(parts) + 1)]
        want = {"cookie_hits": states.count("valid"),
                "cookie_stale": states.count("stale"),
                "cookie_missing": states.count("none") +
                states.count("absent")}
        want["searches"] = want["cookie_stale"] + want["cookie_missing"]
        msg = "%s by handle: %s, kstats %s (cookies %s)" % (rel, got,
              " ".join("%s +%d" % kv for kv in k.items()), "/".join(states))
        why = []
        if got != mnt + "/" + rel:
            why.append("expected " + mnt + "/" + rel)
        if k != want:
            why.append("expected kstats " +
                       " ".join("%s +%d" % kv for kv in want.items()))
        if why:
            print("FAIL: %s: %s" % (msg, ", ".join(why)))
            fails += 1
        else:
            print("ok: " + msg)
    print("%s: kstats %s, %d failures" % (ds, " ".join(
        "%s +%d" % kv for kv in total.items()), fails))
    sys.exit(1 if fails else 0)

{"make": make, "stale": stale, "reuse": reuse, "rename": rename, "v4": v4,
 "check": check}[mode]()
PY

function cookie_check # mode fs [expect]
{
	typeset mode=$1 fs=$2 exp=${3:-$2}
	log_must python3 $CHECK $mode $TESTPOOL/$fs $TESTDIR1/$fs \
	    $TEST_BASE_DIR/exportfs_parent_cookie.$exp.json
}

log_must zfs create -o mountpoint=$TESTDIR1/cs $TESTPOOL/cs
log_must zfs create -o casesensitivity=insensitive \
    -o mountpoint=$TESTDIR1/ci $TESTPOOL/ci

for fs in cs ci; do
	cookie_check make $fs
	cookie_check check $fs
done

# Stale cookies on directories: renames that don't update the cookie, as an
# implementation that doesn't know it does.
for fs in cs ci; do
	log_must set_tunable32 PARENT_COOKIE 0
	cookie_check stale $fs
	log_must set_tunable32 PARENT_COOKIE 1
	cookie_check reuse $fs
	cookie_check check $fs
done

# Received datasets carry the cookies, and incrementals the new ones.
log_must zfs snapshot $TESTPOOL/cs@a
log_must eval "zfs send $TESTPOOL/cs@a | \
    zfs recv -o mountpoint=$TESTDIR1/recv $TESTPOOL/recv"
cookie_check check recv cs
cookie_check rename cs
log_must zfs snapshot $TESTPOOL/cs@b
log_must eval "zfs send -i @a $TESTPOOL/cs@b | zfs recv -F $TESTPOOL/recv"
cookie_check check cs
cookie_check check recv cs

# Objects from before SA have no cookie and are found by searching, also
# after a rename.  Once converted to SA (here by chmod), their next rename
# records a cookie.
log_must zfs create -o version=4 -o mountpoint=$TESTDIR1/v4 $TESTPOOL/v4
log_must eval "mkdir $TESTDIR1/v4/old $TESTDIR1/v4/old/Dir{0..2999}"
log_must zfs upgrade -V 5 $TESTPOOL/v4
log_must eval "mkdir $TESTDIR1/v4/old/New{0..999}"
log_must mv $TESTDIR1/v4/old/Dir7 $TESTDIR1/v4/old/Dir7r
log_must chmod 0750 $TESTDIR1/v4/old/Dir8 $TESTDIR1/v4/old/Dir9
log_must mv $TESTDIR1/v4/old/Dir8 $TESTDIR1/v4/old/Dir8r
cookie_check v4 v4
cookie_check check v4

# The cookie never causes a spill block.  Each step adds 8 bytes to the
# xattrs (a multiple of the SA alignment), so exactly one step fits in the
# bonus buffer without the 8-byte cookie but not with it.
log_must zfs create -o xattr=sa -o dnodesize=1k \
    -o mountpoint=$TESTDIR1/spill $TESTPOOL/spill
typeset -i size
objs=""
for ((size = 8; size <= 800; size += 8)); do
	log_must touch $TESTDIR1/spill/f$size
	log_must set_xattr x "$(printf "%${size}s" "" | tr ' ' a)" \
	    $TESTDIR1/spill/f$size
	objs="$objs $(get_objnum $TESTDIR1/spill/f$size)"
done
log_must zpool sync $TESTPOOL
zdb -ddddd $TESTPOOL/spill $objs > $TEST_BASE_DIR/exportfs_parent_cookie.zdb
log_must awk '
	/^ *Object +lvl/ { getline; obj = $1; n++; spill[n] = 0;
	    cookie[n] = "absent"; next }
	/dnode flags:.*SPILL_BLKPTR/ { spill[n] = 1 }
	/^\tcookie\t/ { cookie[n] = $3 }
	END {
		first = 0
		for (i = 1; i <= n; i++) {
			printf("xattr %d bytes: cookie %s%s\n", i * 8,
			    cookie[i], spill[i] ? ", spill block" : "")
			if (spill[i] && first == 0)
				first = i
		}
		if (n != 100 || first < 3) {
			print "FAIL: expected 100 objects and a spill after " \
			    "the first two"; exit 1
		}
		if (cookie[first - 1] != "absent") {
			print "FAIL: the cookie was kept and caused a spill"
			exit 1
		}
		for (i = 1; i < first - 1; i++) if (cookie[i] != "valid") {
			print "FAIL: cookie left out while it fit"; exit 1
		}
	}' $TEST_BASE_DIR/exportfs_parent_cookie.zdb

log_pass "Parent cookies name their entries and are used, stale ones fall back"
