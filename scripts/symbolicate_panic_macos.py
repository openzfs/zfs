#!/usr/bin/env python3
#
# Symbolicate panic files / stacks on Apple Silicon / arm64
# 
# Common usage may include:
# ./symbolicate_panic.py -p ZFS.2.3.1rc1.kernel.panic.txt 
#    -k /Library/Extensions/zfs.kext/Contents/MacOS/zfs --kernel 
#   --kdk-nearest --accept-mismatch
#
# === Panicked thread (merged, ordered) ===
# 0xfffffe003e489ed0  [kernel] sleh_synchronous (in kernel.release.t6041)
# 0xfffffe0044155ab4  [zfs]    vdev_disk_io_start (in zfs) (vdev_disk.c:750)
# 0xfffffe0044140c54  [zfs]    zio_vdev_io_start (in zfs) (zio.c:0)
#
# Cobbled togther with ChatGPT, and lundman@lundman.net
#

import argparse, os, re, sys, json, glob, shlex, subprocess, tempfile, atexit, shutil
from collections import Counter
from typing import Optional

_TMP_DIRS = []
def _mktemp():
    d = tempfile.mkdtemp(prefix='symbolicate_pkg_')
    _TMP_DIRS.append(d)
    return d
@atexit.register
def _cleanup_tmp():
    for d in _TMP_DIRS:
        shutil.rmtree(d, ignore_errors=True)

HEX = r'0x[0-9a-fA-F]+'

def norm_uuid(u: str) -> str:
    """Strip dashes/case so UUIDs from otool, panic text, and binaryImages compare equal."""
    return re.sub(r'[^0-9A-Fa-f]', '', u or '').upper()

def run(cmd: str):
    return subprocess.run(cmd, shell=True, check=False,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def macho_text_info(path: str):
    """Return (__TEXT vmaddr, __TEXT_EXEC vmaddr, __TEXT_EXEC vmsize) as ints."""
    out = run(f'otool -l {shlex.quote(path)}')
    if out.returncode != 0:
        sys.exit(f"otool failed for {path}:\n{out.stderr}")
    text = exec_ = size = None
    in_text = in_exec = False
    for line in out.stdout.splitlines():
        line = line.strip()
        if line.startswith('segname '):
            name = line.split()[1]
            in_text = (name == '__TEXT')
            in_exec = (name == '__TEXT_EXEC')
        elif line.startswith('vmaddr'):
            v = int(line.split()[1], 16)
            if in_text and text is None: text = v
            if in_exec and exec_ is None: exec_ = v
        elif line.startswith('vmsize') and in_exec and size is None:
            size = int(line.split()[1], 16)
    if text is None or exec_ is None or size is None:
        sys.exit(f"Could not read __TEXT/__TEXT_EXEC from {path}")
    return text, exec_, size

def macho_arch(path: str):
    out = run(f'lipo -info {shlex.quote(path)}')
    s = out.stdout
    if 'arm64e' in s: return 'arm64e'
    if 'arm64' in s:  return 'arm64'
    return 'arm64'

def macho_uuid(path: str) -> str:
    r = run(f'otool -l {shlex.quote(path)} | sed -n "s/^ *uuid //p"')
    return (r.stdout.strip().splitlines() or [""])[0]

def iter_pkg_paths(paths):
    """Expand each --pkg argument (a .pkg file or a directory) into .pkg files."""
    out = []
    for p in paths or []:
        if os.path.isdir(p):
            out += sorted(glob.glob(os.path.join(p, '*.pkg')))
        elif os.path.isfile(p):
            out.append(p)
        else:
            print(f"[pkg] not found, skipping: {p}")
    return out

def zfs_binaries_from_pkg(pkg: str, leaf: str = 'zfs'):
    """Expand a flat product-archive .pkg and return [(UUID, path), ...] for every
       …/<name>.kext/Contents/MacOS/<leaf> Mach-O found in its Payload(s)."""
    d = _mktemp()
    r = run(f'xar -C {shlex.quote(d)} -xf {shlex.quote(pkg)}')
    if r.returncode != 0:
        print(f"[pkg] xar failed on {pkg}:\n{r.stderr.strip()}")
        return []
    found = []
    for payload in glob.glob(os.path.join(d, '**', 'Payload'), recursive=True):
        ex = os.path.join(os.path.dirname(payload), 'extract')
        os.makedirs(ex, exist_ok=True)
        # Payload is a compressed cpio (gzip/xz/lzma) or, rarely, a bare cpio.
        run(f'cd {shlex.quote(ex)} && ( gzip -dc {shlex.quote(payload)} 2>/dev/null '
            f'|| xz -dc {shlex.quote(payload)} 2>/dev/null '
            f'|| cat {shlex.quote(payload)} ) | cpio -id 2>/dev/null')
        for z in glob.glob(os.path.join(ex, '**', f'*.kext/Contents/MacOS/{leaf}'),
                           recursive=True):
            found.append(((macho_uuid(z) or '').upper(), z))
    return found

def kext_from_pkgs_by_image_uuids(pkg_args, image_by_uuid: dict, leaf: str = 'zfs'):
    """Like kext_from_pkgs, but matches against the compact stackshot's
       binaryImages UUID set instead of a classic '<bundle>[UUID]@base->end'
       table. Newer paniclogs (paniclog version 16+, macOS 26/27) drop that
       table entirely and only carry binaryImages: [[uuid, base, flag], ...]
       with no bundle-id column, so we can't pick a UUID up front -- instead
       we check each candidate zfs binary's own UUID against every image in
       the stackshot and take whichever one is actually present.
       Returns (path, uuid) or (None, None)."""
    cand = []
    for pkg in iter_pkg_paths(pkg_args):
        for uuid, path in zfs_binaries_from_pkg(pkg, leaf):
            cand.append((uuid, path, pkg))
    if not cand:
        print("[pkg] no zfs binaries found in the given pkg(s).")
        return None, None
    for uuid, path, pkg in cand:
        if norm_uuid(uuid) in image_by_uuid:
            print(f"[pkg] UUID match {uuid}  <-  {os.path.basename(pkg)}  (via binaryImages)")
            return path, uuid
    print("[pkg] no candidate binary's UUID appears in this panic's binaryImages; candidates were:")
    for uuid, path, pkg in cand:
        print(f"[pkg]   {uuid}  ({os.path.basename(pkg)})")
    return None, None

def kext_from_pkgs(pkg_args, want_uuid: str, leaf: str = 'zfs'):
    """Pick the zfs binary whose UUID matches the panic from the given pkg(s)/dir(s).
       Returns a path, or None (after printing what it did find)."""
    want = (want_uuid or '').upper()
    cand = []
    for pkg in iter_pkg_paths(pkg_args):
        for uuid, path in zfs_binaries_from_pkg(pkg, leaf):
            cand.append((uuid, path, pkg))
    if not cand:
        print("[pkg] no zfs binaries found in the given pkg(s).")
        return None
    if want:
        for uuid, path, pkg in cand:
            if uuid == want:
                print(f"[pkg] UUID match {want}  <-  {os.path.basename(pkg)}")
                return path
    print(f"[pkg] no binary matches panic UUID {want or '(unknown)'}; candidates were:")
    for uuid, path, pkg in cand:
        print(f"[pkg]   {uuid}  ({os.path.basename(pkg)})")
    return None

def decode_esr(esr: int) -> str:
    ec  = (esr >> 26) & 0x3f
    dfsc = esr & 0x3f
    EC = {
        0x24: "Data Abort, lower EL",
        0x25: "Data Abort, same EL",
        0x20: "Instruction Abort, lower EL",
        0x21: "Instruction Abort, same EL",
    }.get(ec, f"EC=0x{ec:x}")
    DFSC = {
        0x04: "Translation fault, level 0",
        0x05: "Translation fault, level 1",
        0x06: "Translation fault, level 2",
        0x07: "Translation fault, level 3",
        0x0d: "Permission fault, level 1",
        0x0e: "Permission fault, level 2",
        0x0f: "Permission fault, level 3",
        0x11: "Alignment fault",
    }.get(dfsc, f"DFSC=0x{dfsc:x}")
    return f"{EC}; {DFSC}"

def atos_one(obj: str, arch: str, text_load_hex: str, addr_hex: str) -> str:
    r = run(f'/usr/bin/atos -o {shlex.quote(obj)} -arch {arch} -l {text_load_hex} {addr_hex}')
    if r.returncode != 0: return ''
    return r.stdout.strip()

def depac(addr: int) -> int:
    top = (addr >> 56) & 0xff
    if top not in (0xff, 0x00):
        addr &= 0x00ffffffffffffff
    return addr

def find_kdk(build: str):
    root = "/Library/Developer/KDKs"
    if not os.path.isdir(root): return None
    cands = sorted([d for d in glob.glob(os.path.join(root, f"*{build}*")) if os.path.isdir(d)])
    return cands[-1] if cands else None

def parse_build_parts(build: str):
    m = re.match(r'(\d+)([A-Z])(\d+)$', build or '')
    return (int(m.group(1)), m.group(2), int(m.group(3))) if m else None

def find_kdk_nearest(build: str):
    parts = parse_build_parts(build)
    if not parts: return None
    tn, tl, p = parts
    root = "/Library/Developer/KDKs"
    best = None; bestdiff = 1<<30
    for d in sorted(os.listdir(root)):
        if not d.startswith("KDK_"): continue
        m = re.search(r'([0-9]+)([A-Z])([0-9]+)', d)
        if not m: continue
        tn2, tl2, p2 = int(m.group(1)), m.group(2), int(m.group(3))
        if tn2==tn and tl2==tl:
            diff = abs(p2-p)
            if diff < bestdiff:
                best = os.path.join(root, d); bestdiff = diff
    return best

def kernel_paths_from_kdk(kdk_dir: str, soc_hint: Optional[str] = None,
                          prefer_release: bool = True, allow_kasan: bool = False):
    base = os.path.join(kdk_dir, "System/Library/Kernels")
    if not os.path.isdir(base): return None
    names = [n for n in os.listdir(base) if n.startswith("kernel")]
    if not allow_kasan:
        names = [n for n in names if ".kasan" not in n]

    def pick(order):
        for pat in order:
            for n in names:
                if n == pat:
                    return os.path.join(base, n)
        return None

    order = []
    if soc_hint:
        if prefer_release:
            order += [f"kernel.release.{soc_hint}", f"kernel.development.{soc_hint}"]
        else:
            order += [f"kernel.development.{soc_hint}", f"kernel.release.{soc_hint}"]
    if prefer_release:
        order += ["kernel.release", "kernel.development"]
    else:
        order += ["kernel.development", "kernel.release"]
    order += [n for n in names if n.startswith("kernel.development.")]
    order += ["kernel"]

    sel = pick(order)
    if sel: return sel
    # last resort: any (non-kasan) kernel
    return os.path.join(base, sorted(names)[0]) if names else None

def parse_panic(path: str, bundle: str):
    raw = open(path, 'r', errors='ignore').read()
    txt = raw
    build = ''
    kern_text_exec_base = None
    kern_text_base = None
    kernel_uuid = ''
    soc_hint = None

    # .ips JSON?
    # Some panic files contain two JSON objects: a one-line header followed by
    # the main object.  json.loads() rejects that, so try stripping the header.
    full_json = None
    image_by_uuid = {}
    if raw.lstrip().startswith('{') and '"panicString"' in raw:
        j = None
        for candidate in [raw, raw.split('\n', 1)[-1]]:
            try:
                j = json.loads(candidate)
                break
            except Exception:
                pass
        if j:
            full_json = j
            txt = j.get('panicString', '') or raw
            osv = j.get('os_version') or ''
            m = re.search(r'Build\s+([0-9A-Za-z]+)', osv)
            if m: build = m.group(1)
            # Newer paniclogs (version 16+) drop the classic per-kext
            # '<bundle>[UUID]@base->end' table and instead carry a compact
            # stackshot: binaryImages is [[uuid, base, flag], ...] with no
            # name column, and processByPid[*].threadById[*].kernelFrames
            # references it by index. Index it here so main() can match a
            # candidate zfs binary's own UUID against it later.
            bi = j.get('binaryImages')
            if isinstance(bi, list):
                for idx, entry in enumerate(bi):
                    try:
                        uuid_s, base = entry[0], entry[1]
                    except Exception:
                        continue
                    image_by_uuid.setdefault(norm_uuid(uuid_s), []).append(
                        (idx, int(base) & 0xFFFFFFFFFFFFFFFF))

    if not build:
        m_build = re.search(r'OS version:\s*([0-9A-Za-z]+)', txt)
        build = m_build.group(1) if m_build else ''
    m_ktexec = re.search(r'Kernel text exec base:\s*('+HEX+')', txt)
    if m_ktexec: kern_text_exec_base = int(m_ktexec.group(1),16)
    m_ktxt = re.search(r'Kernel text base:\s*('+HEX+')', txt)
    if m_ktxt: kern_text_base = int(m_ktxt.group(1),16)
    m_uuid = re.search(r'Kernel UUID:\s*([0-9A-Fa-f-]+)', txt)
    if m_uuid: kernel_uuid = m_uuid.group(1)
    m_soc = re.search(r'RELEASE_ARM64_T(\d+)', txt)
    if m_soc: soc_hint = f"t{m_soc.group(1)}"
    if not soc_hint:
        m_soc2 = re.search(r'AppleT(\d+)', txt)
        if m_soc2: soc_hint = f"t{m_soc2.group(1)}"
    is_release = bool(re.search(r'\bRELEASE_ARM64', txt))


    # kext base/end (+ UUID, so we can verify -k points at the right binary)
    kext_base = kext_end = None
    kext_uuid = ''
    UUIDRE = r'[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}'
    for line in txt.splitlines():
        if bundle in line and '@' in line and '->' in line:
            m = re.search(r'@('+HEX+')->('+HEX+')', line)
            if m:
                kext_base = int(m.group(1),16)
                kext_end  = int(m.group(2),16)
            mu = re.search(r'\[('+UUIDRE+r')\]', line)
            if mu:
                kext_uuid = mu.group(1)
            if kext_base is not None:
                break

    # Ordered panicked-thread frames: pc then each lr in order
    ordered = []
    m_pc = re.search(r'\bpc:\s*('+HEX+')', txt)
    if m_pc:
        ordered.append(int(m_pc.group(1),16))
    m_bt = re.search(r'Panicked thread.*?backtrace:\s*(.*?)(?:\n\s*\n|\Z)', txt, re.DOTALL)
    if m_bt:
        for line in m_bt.group(1).splitlines():
            m = re.search(r'lr:\s*('+HEX+')', line)
            if m:
                ordered.append(int(m.group(1),16))
    # Fallback: include any 12–16 hex addr tokens if no ordered frames
    addrs = [int(x,16) for x in re.findall(r'0x[0-9a-fA-F]{12,16}', txt)]
    addrs = sorted(set(depac(a) for a in addrs))

    m_esr = re.search(r'esr:\s*(0x[0-9a-fA-F]+)', txt); esr = int(m_esr.group(1),16) if m_esr else None
    m_far = re.search(r'far:\s*(0x[0-9a-fA-F]+)', txt); far = int(m_far.group(1),16) if m_far else None

    return {
        'build': build,
        'kern_text_exec_base': kern_text_exec_base,
        'kern_text_base': kern_text_base,
        'kernel_uuid': kernel_uuid,
        'kext_base': kext_base,
        'kext_end': kext_end,
        'kext_uuid': kext_uuid,
        'soc_hint': soc_hint,
        'ordered': ordered,
        'addrs': addrs,
        'is_release': is_release,
        'esr': esr,
        'far': far,
        'full_json': full_json,
        'image_by_uuid': image_by_uuid,
    }

def choose_mapping_for_kext(dwarf_obj: str, arch: str,
                            panic_base_hex: str, text_vm: int, exec_vm: int, exec_size: int,
                            candidates: list):
    delta = exec_vm - text_vm
    base = int(panic_base_hex,16)
    # A) panic@=__TEXT, B) panic@=__TEXT_EXEC
    text_load_A = base
    text_load_B = base - delta
    exec_load_A = text_load_A + delta
    exec_load_B = text_load_B + delta

    def in_code(a, exec_load): return exec_load <= a < (exec_load + exec_size)
    probe = next((a for a in candidates if in_code(a, exec_load_A or exec_load_B)), None)
    if probe is None and candidates:
        probe = candidates[0]

    chosen = None
    for tl, el in ((text_load_B, exec_load_B), (text_load_A, exec_load_A)):
        if probe is None: break
        s = atos_one(dwarf_obj, arch, hex(tl), hex(probe))
        if s and not re.fullmatch(r'0x[0-9a-fA-F]+', s):
            chosen = (tl, el)
            break
    if chosen is None:
        chosen = (text_load_B, exec_load_B)
    return hex(chosen[0]), chosen[1], delta

def symbolicate(obj: str, arch: str, text_load_hex: str, addr: int) -> str:
    out = atos_one(obj, arch, text_load_hex, hex(addr))
    if not out or re.fullmatch(r'0x[0-9a-fA-F]+', out):
        return '(no symbol)'
    return out

def main():
    ap = argparse.ArgumentParser(description="Symbolicate macOS panic for ZFS kext and kernel (KDK).")
    ap.add_argument('-p','--panic', help='.panic or .ips path')
    ap.add_argument('-k','--kext', help='Path to zfs Mach-O (…/zfs.kext/Contents/MacOS/zfs). '
                                        'Optional if --pkg is given (binary is picked by UUID).')
    ap.add_argument('--pkg', action='append', metavar='PKG_OR_DIR',
                    help='OpenZFSonOsX .pkg file or a directory of them. The zfs binary whose '
                         'UUID matches the panic is extracted and used. Repeatable. Needs -p.')
    ap.add_argument('-d','--dwarf', help='Optional DWARF file (.dSYM/…/DWARF/zfs). Default=kext binary.')
    ap.add_argument('-b','--base', help='KEXT base address (hex). If omitted, read from panic.')
    ap.add_argument('-i','--bundle', default='org.openzfsonosx.zfs', help='Kext bundle id in panic (default zfs).')
    ap.add_argument('--kernel', action='store_true', help='Also symbolicate kernel frames via KDK.')
    ap.add_argument('--kdk', help='Path to a KDK directory OR directly to a kernel image.')
    ap.add_argument('--kdk-nearest', action='store_true', help='If exact KDK not found, pick nearest same-train.')
    ap.add_argument('--accept-mismatch', action='store_true', help='Proceed even if KDK build mismatches.')
    ap.add_argument('--soc', help='Force SoC for kernel selection (e.g., t6041).')
    ap.add_argument('--prefer-release', action='store_true', help='Prefer kernel.release.* over development.')
    ap.add_argument('--allow-kasan', action='store_true', help='Allow picking KASAN kernels.')
    args, extra_addrs = ap.parse_known_args()

    # Panic parse first: its kext UUID is what lets --pkg pick the right binary.
    pi = parse_panic(args.panic, args.bundle) if args.panic else {
        'build':'','kern_text_exec_base':None,'kern_text_base':None,'kernel_uuid':'',
        'kext_base':None,'kext_end':None,'kext_uuid':'','soc_hint':None,'ordered':[],'addrs':[],
        'full_json':None,'image_by_uuid':{}
    }

    # KEXT setup: explicit -k, else extract the UUID-matching binary from --pkg.
    kext = args.kext
    if not kext and args.pkg:
        if not args.panic:
            sys.exit("--pkg needs a panic (-p) to match a UUID against.")
        if pi.get('kext_uuid'):
            kext = kext_from_pkgs(args.pkg, pi['kext_uuid'])
        if not kext and pi.get('image_by_uuid'):
            kext, matched_uuid = kext_from_pkgs_by_image_uuids(args.pkg, pi['image_by_uuid'])
            if kext and not pi.get('kext_uuid'):
                pi['kext_uuid'] = matched_uuid
        if not kext:
            sys.exit(" No matching binary in --pkg; supply the right pkg or pass -k explicitly.")
    if not kext:
        sys.exit("Provide -k <zfs binary>, or --pkg <pkg/dir> together with -p <panic>.")

    dwarf = args.dwarf or kext
    arch  = macho_arch(kext)
    text_vm, exec_vm, exec_size = macho_text_info(kext)

    # Compact-stackshot image index for this kext binary, if this panic's
    # binaryImages carries its UUID (newer paniclogs -- see parse_panic()).
    # Used both as a base-address fallback and to scan processByPid for
    # threads actually executing in this kext.
    kext_image_index = None
    kext_image_base = None
    image_entries = pi.get('image_by_uuid', {}).get(norm_uuid(macho_uuid(kext)))
    if image_entries:
        kext_image_index, kext_image_base = image_entries[0]

    # Every thread (any pid, incl. kernel_task) with a frame inside this kext
    # image -- the only place a hang like a busy-timeout/watchdog panic shows
    # *our* code, since the panicking thread there belongs to watchdogd, not
    # zfs. Gathered before the base checks below so it can still be reported
    # even when the base turns out to be unusable.
    kext_thread_hits = []
    if pi.get('full_json') and kext_image_index is not None:
        pbp = pi['full_json'].get('processByPid', {}) or {}
        for pidstr, proc in pbp.items():
            for tid, th in (proc.get('threadById') or {}).items():
                kf = th.get('kernelFrames') or th.get('userFrames')
                if not kf:
                    continue
                if any(isinstance(f, (list, tuple)) and len(f) >= 2 and f[0] == kext_image_index
                       for f in kf):
                    kext_thread_hits.append((pidstr, proc.get('procname'), tid, th, kf))

    base_hex = args.base or (pi['kext_base'] and hex(pi['kext_base']))
    if not base_hex and image_entries and kext_image_base:
        base_hex = hex(kext_image_base)
        print(f"[binaryImages] no classic kext table in this panic; resolved base "
              f"{base_hex} from the compact stackshot (image #{kext_image_index})")

    if not base_hex:
        # A "T"-flagged image with base 0x0 in this compact schema is what a
        # 3rd-party/AuxKC kext looks like: unlike the boot kernelcache and
        # built-in kexts, its runtime slide isn't recorded here, so every
        # frame naming it also comes through as offset 0 -- there's no real
        # location to symbolicate, only "some thread was somewhere in this
        # kext". Report that plainly (grouped, deduped) instead of either
        # silently computing garbage from address 0, or calling atos once per
        # matching thread (which is what turned this into a multi-minute
        # subprocess-spawning loop against ~2000 threads).
        if kext_thread_hits:
            if image_entries:
                print(f"[binaryImages] {args.bundle} is image #{kext_image_index} in this "
                      f"panic's compact stackshot, but its recorded base is 0x0 -- no real "
                      f"load address, so per-thread offsets can't be symbolicated. This is "
                      f"expected for 3rd-party/AuxKC kexts in this stackshot format.")
            distinct_shapes = len({tuple(map(tuple, kf)) for *_, kf in kext_thread_hits})
            names = Counter(th.get('name') for _, _, _, th, _ in kext_thread_hits)
            print(f"\n=== {len(kext_thread_hits)} thread(s) reference {args.bundle} "
                  f"image #{kext_image_index} (no usable offsets, {distinct_shapes} "
                  f"distinct stack shapes) ===")
            print(" Top thread names (not evidence of a hang by itself -- with base 0x0 "
                  "every frame naming this image reads the same regardless of thread, and "
                  "in a panic this size that can mean most of the system's threads match):")
            for name, cnt in names.most_common(25):
                print(f"  {cnt:5d}x  {name!r}")
            print("\n Pass -b <real base hex> if you have it from another source (e.g. "
                  "`kextstat`/`kmutil` output taken close to the panic) to get real symbols.")
        sys.exit("No usable kext base address (-b, classic table, and binaryImages all empty/zero).")

    # Candidate addresses (for mapping decision); ordered list for final output
    addrs = []
    if extra_addrs:
        for s in extra_addrs:
            try: addrs.append(depac(int(s,16)))
            except: pass
    elif args.panic:
        addrs = pi['addrs']
    ordered = [depac(a) for a in pi.get('ordered', [])] or addrs

    # Stackshot frames known to be in this kext image make good probes for
    # picking __TEXT vs __TEXT_EXEC mapping too -- the classic text-derived
    # addrs/ordered lists are empty/irrelevant for this schema (they come
    # from the panicking thread, which is watchdogd/kernel here, not zfs).
    stackshot_addrs = sorted({int(base_hex, 16) + off
                               for _, _, _, _, kf in kext_thread_hits
                               for imgidx, off in kf if imgidx == kext_image_index})

    # Choose kext mapping
    text_load_hex, kexec_load, kdelta = choose_mapping_for_kext(
        dwarf, arch, base_hex, text_vm, exec_vm, exec_size,
        (addrs or ordered) + stackshot_addrs
    )

    # Verify the -k binary actually matches the kext that panicked.
    # A UUID mismatch here is the classic "symbolicated the wrong binary"
    # trap: atos still emits confident, wrong symbol+offset for every frame.
    kext_file_uuid  = (macho_uuid(kext) or '').upper()
    panic_kext_uuid = (pi.get('kext_uuid') or '').upper()

    print("=== KEXT mapping ===")
    print(f" bundle: {args.bundle}")
    print(f" arch:   {arch}")
    print(f" base@:  {base_hex}")
    print(f" panic {args.bundle} UUID: {panic_kext_uuid or 'unknown'}")
    print(f" kext file UUID:          {kext_file_uuid or 'unknown'}")
    if panic_kext_uuid and kext_file_uuid and panic_kext_uuid != kext_file_uuid:
        print()
        print(" *** UUID MISMATCH ***")
        print(f"   -k binary : {kext}")
        print(f"   file UUID : {kext_file_uuid}")
        print(f"   panic UUID: {panic_kext_uuid}")
        print("   Every [zfs] symbol below would be WRONG (nearest-symbol + bogus offset).")
        print("   Point -k at the zfs binary whose UUID matches the panic, e.g.:")
        print("     for f in $(find . -path '*zfs.kext*' -name zfs -type f); do")
        print(f'       echo "$(otool -l \\"$f\\" | sed -n \'s/^ *uuid //p\')  $f"; done | grep -i {panic_kext_uuid}')
        if not args.accept_mismatch:
            sys.exit(" Refusing to symbolicate. Re-run with --accept-mismatch to override.")
        print(" --accept-mismatch given: proceeding anyway (results are untrustworthy).")
    elif panic_kext_uuid and kext_file_uuid:
        print(" UUID match: OK")
    else:
        print(" (UUID check skipped: no panic UUID and/or unreadable kext UUID)")
    print(f" file __TEXT vmaddr:      {hex(text_vm)}")
    print(f" file __TEXT_EXEC vmaddr: {hex(exec_vm)}   delta={hex(exec_vm-text_vm)}")
    print(f" chosen TEXT_LOAD:        {text_load_hex}\n")

    # Kernel mapping (optional)
    kernel_ready = False
    k_text_load_hex = None
    k_exec_load = None
    k_exec_size = None
    kern_img = None


    if args.kernel:
        build = pi['build']; ktexec = pi['kern_text_exec_base']; soc_hint = args.soc or pi.get('soc_hint')
        print("=== Kernel mapping (via KDK) ===")
        if not build or not ktexec:
            print(" checking for KDK: skipped (missing OS build or kernel text exec base in panic)")
            print(" Hint: pass -p with full .panic/.ips.")
        else:
            used_kdk = None; match = False
            prefer_release = args.prefer_release or pi.get('is_release', False)
            if args.kdk:
                if os.path.isdir(args.kdk):
                    used_kdk = args.kdk
                    kern_img = kernel_paths_from_kdk(used_kdk, soc_hint, prefer_release, args.allow_kasan)
                elif os.path.isfile(args.kdk):
                    kern_img = args.kdk
                else:
                    print(f" --kdk path not found: {args.kdk}")
            if not kern_img and not used_kdk:
                exact = find_kdk(build)
                if exact:
                    used_kdk = exact; match = True
                elif args.kdk_nearest:
                    used_kdk = find_kdk_nearest(build)
                if used_kdk:
                    kern_img = kernel_paths_from_kdk(used_kdk, soc_hint, prefer_release, args.allow_kasan)

            print(f" checking for KDK build {build} ... ", end="")
            if kern_img:
                if used_kdk:
                    print(("found exact: " if match else "nearest: ") + used_kdk)
                else:
                    print("using supplied kernel image")
            else:
                print("not found")
                print(" Hint: install KDK or pass --kdk <KDK dir> or --kdk <…/kernel(.release|.development).tXXXX>")
            if kern_img:
                if used_kdk and (not match) and (not args.accept_mismatch):
                    print(" Warning: KDK build mismatch; use --accept-mismatch to proceed.")
                try:
                    kt, kx, ksz = macho_text_info(kern_img)
                except SystemExit as e:
                    print(f" otool failed on: {kern_img}")
                    print(" List kernels and pick the SoC one:")
                    print(f"   ls -1 {os.path.dirname(kern_img)}")
                    return
                kdelta2 = kx - kt
                k_text_load_hex = hex(ktexec - kdelta2)
                k_exec_load = ktexec
                k_exec_size = ksz
                kernel_ready = True

                print(f" build: {build}")
                print(f" SoC:   {soc_hint or 'unknown'}")
                print(f" kernel: {kern_img}")
                print(f" panic Kernel UUID:          {pi.get('kernel_uuid') or 'unknown'}")
                print(f" KDK Kernel UUID:            {macho_uuid(kern_img) or 'unknown'}")
                print(f" file __TEXT vmaddr:      {hex(kt)}")
                print(f" file __TEXT_EXEC vmaddr: {hex(kx)}   delta={hex(kdelta2)}")
                print(f" panic Kernel text exec base: {hex(ktexec)}")
                print(f" chosen TEXT_LOAD:           {k_text_load_hex}")
                if pi.get('esr') is not None:
                    print(f" ESR: 0x{pi['esr']:08x}  -> {decode_esr(pi['esr'])}")
                if pi.get('far') is not None:
                    print(f" FAR: 0x{pi['far']:016x}")
                print(f"\n")

    # ---- Merged, ordered panicked-thread backtrace ----
    print("=== Panicked thread (merged, ordered) ===")
    seen = set()
    lines = []
    for a in ordered:
        if a in seen: continue
        seen.add(a)

        # Decide image: kext exec range first, then kernel
        in_kext = (kexec_load <= a < (kexec_load + exec_size))
        in_kern = (kernel_ready and k_exec_load is not None and (k_exec_load <= a < (k_exec_load + k_exec_size)))

        if in_kext:
            sym = symbolicate(dwarf, arch, text_load_hex, a)
            lines.append(f"{hex(a)}  [zfs]    {sym}")
        elif in_kern:
            sym = symbolicate(kern_img, 'arm64e', k_text_load_hex, a)
            lines.append(f"{hex(a)}  [kernel] {sym}")
        else:
            lines.append(f"{hex(a)}  (unknown image)")

    for line in reversed(lines):
        print(line)

    # ---- Threads executing inside [zfs] per the compact stackshot ----
    # For a watchdog/busy-timeout style panic, the *panicking* thread above
    # belongs to watchdogd/launchd, not zfs -- whatever's actually stuck in
    # our kext only shows up here, as some other thread's kernelFrames.
    if pi.get('full_json') and kext_image_index is not None:
        print(f"\n=== Threads with a frame in [zfs] image #{kext_image_index} (compact stackshot) ===")
        if not kext_thread_hits:
            print(" (none -- nothing was executing in this kext at snapshot time)")
        else:
            # Idle worker threads (taskq, condvar waits, ...) tend to share the
            # exact same frame tuple by the hundreds/thousands -- symbolicate
            # each distinct tuple once, not once per thread. That's what turned
            # this into a multi-minute atos-spawning loop before.
            sym_cache = {}
            def sym_for(imgidx, off):
                key = (imgidx, off)
                if key not in sym_cache:
                    if imgidx == kext_image_index:
                        addr = int(base_hex, 16) + off
                        sym_cache[key] = f"[zfs]      +{hex(off)}  {symbolicate(dwarf, arch, text_load_hex, addr)}"
                    else:
                        sym_cache[key] = f"[image #{imgidx}] +{hex(off)}"
                return sym_cache[key]

            by_frames = {}
            for pidstr, procname, tid, th, kf in kext_thread_hits:
                key = tuple(map(tuple, kf))
                by_frames.setdefault(key, []).append((pidstr, procname, tid, th))

            print(f" {len(kext_thread_hits)} thread(s), {len(by_frames)} distinct stack shape(s)")
            for kf, threads in sorted(by_frames.items(), key=lambda kv: -len(kv[1])):
                names = Counter(th.get('name') for _, _, _, th in threads)
                summary = ", ".join(f"{n!r}x{c}" if c > 1 else repr(n) for n, c in names.most_common(6))
                if len(names) > 6:
                    summary += ", ..."
                print(f"\n -- {len(threads)} thread(s): {summary}")
                for imgidx, off in kf:
                    print(f"   {sym_for(imgidx, off)}")

if __name__ == "__main__":
    main()
