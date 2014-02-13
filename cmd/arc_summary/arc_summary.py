#!/usr/bin/python
#
# $Id: arc_summary.pl,v 388:e27800740aa2 2011-07-08 02:53:29Z jhell $
#
# Copyright (c) 2008 Ben Rockwood <benr@cuddletech.com>,
# Copyright (c) 2010 Martin Matuska <mm@FreeBSD.org>,
# Copyright (c) 2010-2011 Jason J. Hellenthal <jhell@DataIX.net>,
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# If you are having troubles when using this script from cron(8) please try
# adjusting your PATH before reporting problems.
#
# /usr/bin & /sbin
#
# Binaries used are:
#
# dc(1), kldstat(8), sed(1), sysctl(8) & vmstat(8)
#
# Binaries that I am working on phasing out are:
#
# dc(1) & sed(1)

import sys
import time
import getopt
import re

from subprocess import Popen, PIPE
from decimal import Decimal as D


usetunable = True
show_sysctl_descriptions = False
alternate_sysctl_layout = False
kstat_pobj = re.compile("^([^:]+):\s+(.+)\s*$", flags=re.M)


def get_Kstat():
    def load_proc_kstats(fn, namespace):
        kstats = [line.strip() for line in open(fn)]
        del kstats[0:2]
        for kstat in kstats:
            kstat = kstat.strip()
            name, unused, value = kstat.split()
            Kstat[namespace + name] = D(value)

    Kstats = [
        "hw.pagesize",
        "hw.physmem",
        "kern.maxusers",
        "vm.kmem_map_free",
        "vm.kmem_map_size",
        "vm.kmem_size",
        "vm.kmem_size_max",
        "vm.kmem_size_min",
        "vm.kmem_size_scale",
        "vm.stats",
        "vm.swap_total",
        "vm.swap_reserved",
        "kstat.zfs",
        "vfs.zfs"
    ]

    #sysctls = " ".join(str(x) for x in Kstats)
    #p = Popen("/sbin/sysctl -q %s" % sysctls, stdin=PIPE,
    #    stdout=PIPE, stderr=PIPE, shell=True, close_fds=True)
    #p.wait()

    Kstat = {}
    load_proc_kstats('/proc/spl/kstat/zfs/arcstats', 'kstat.zfs.misc.arcstats.')
    load_proc_kstats('/proc/spl/kstat/zfs/zfetchstats', 'kstat.zfs.misc.zfetchstats.')
    load_proc_kstats('/proc/spl/kstat/zfs/vdev_cache_stats', 'kstat.zfs.misc.vdev_cache_stats.')

    return Kstat

def div1():
    sys.stdout.write("\n")
    for i in xrange(18):
        sys.stdout.write("%s" % "----")
    sys.stdout.write("\n")


def div2():
    div1()
    sys.stdout.write("\n")


def fBytes(Bytes=0, Decimal=2):
    kbytes = (2 ** 10)
    mbytes = (2 ** 20)
    gbytes = (2 ** 30)
    tbytes = (2 ** 40)
    pbytes = (2 ** 50)
    ebytes = (2 ** 60)
    zbytes = (2 ** 70)
    ybytes = (2 ** 80)

    if Bytes >= ybytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / ybytes) + "\tYiB"
    elif Bytes >= zbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / zbytes) + "\tZiB"
    elif Bytes >= ebytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / ebytes) + "\tEiB"
    elif Bytes >= pbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / pbytes) + "\tPiB"
    elif Bytes >= tbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / tbytes) + "\tTiB"
    elif Bytes >= gbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / gbytes) + "\tGiB"
    elif Bytes >= mbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / mbytes) + "\tMiB"
    elif Bytes >= kbytes:
        return str("%0." + str(Decimal) + "f") % (Bytes / kbytes) + "\tKiB"
    elif Bytes == 0:
        return str("%d" % 0) + "\tBytes"
    else:
        return str("%d" % Bytes) + "\tBytes"


def fHits(Hits=0, Decimal=2):
    khits = (10 ** 3)
    mhits = (10 ** 6)
    bhits = (10 ** 9)
    thits = (10 ** 12)
    qhits = (10 ** 15)
    Qhits = (10 ** 18)
    shits = (10 ** 21)
    Shits = (10 ** 24)

    if Hits >= Shits:
        return str("%0." + str(Decimal) + "f") % (Hits / Shits) + "S"
    elif Hits >= shits:
        return str("%0." + str(Decimal) + "f") % (Hits / shits) + "s"
    elif Hits >= Qhits:
        return str("%0." + str(Decimal) + "f") % (Hits / Qhits) + "Q"
    elif Hits >= qhits:
        return str("%0." + str(Decimal) + "f") % (Hits / qhits) + "q"
    elif Hits >= thits:
        return str("%0." + str(Decimal) + "f") % (Hits / thits) + "t"
    elif Hits >= bhits:
        return str("%0." + str(Decimal) + "f") % (Hits / bhits) + "b"
    elif Hits >= mhits:
        return str("%0." + str(Decimal) + "f") % (Hits / mhits) + "m"
    elif Hits >= khits:
        return str("%0." + str(Decimal) + "f") % (Hits / khits) + "k"
    elif Hits == 0:
        return str("%d" % 0)
    else:
        return str("%d" % Hits)


def fPerc(lVal=0, rVal=0, Decimal=2):
    if rVal > 0:
        return str("%0." + str(Decimal) + "f") % (100 * (lVal / rVal)) + "%"
    else:
        return str("%0." + str(Decimal) + "f") % 100 + "%"


def get_system_memory(Kstat):
    def mem_rounded(mem_size):
        chip_size = 1
        chip_guess = (int(mem_size) / 8) - 1
        while chip_guess != 0:
            chip_guess >>= 1
            chip_size <<= 1

        mem_round = (int(mem_size / chip_size) + 1) * chip_size
        return mem_round

    output = {}

    pagesize = Kstat["hw.pagesize"]
    mem_hw = mem_rounded(Kstat["hw.physmem"])
    mem_phys = Kstat["hw.physmem"]
    mem_all = Kstat["vm.stats.vm.v_page_count"] * pagesize
    mem_wire = Kstat["vm.stats.vm.v_wire_count"] * pagesize
    mem_active = Kstat["vm.stats.vm.v_active_count"] * pagesize
    mem_inactive = Kstat["vm.stats.vm.v_inactive_count"] * pagesize
    mem_cache = Kstat["vm.stats.vm.v_cache_count"] * pagesize
    mem_free = Kstat["vm.stats.vm.v_free_count"] * pagesize

    mem_gap_vm = mem_all - (
        mem_wire + mem_active + mem_inactive + mem_cache + mem_free
        )

    mem_total = mem_hw
    mem_avail = mem_inactive + mem_cache + mem_free
    mem_used = mem_total - mem_avail
    output["active"] = {
        'per': fPerc(mem_active, mem_all),
        'num': fBytes(mem_active),
    }
    output["inact"] = {
        'per': fPerc(mem_inactive, mem_all),
        'num': fBytes(mem_inactive),
    }
    output["wired"] = {
        'per': fPerc(mem_wire, mem_all),
        'num': fBytes(mem_wire),
    }
    output["cache"] = {
        'per': fPerc(mem_cache, mem_all),
        'num': fBytes(mem_cache),
    }
    output["free"] = {
        'per': fPerc(mem_free, mem_all),
        'num': fBytes(mem_free),
    }
    output["gap"] = {
        'per': fPerc(mem_gap_vm, mem_all),
        'num': fBytes(mem_gap_vm),
    }
    output["real_installed"] = fBytes(mem_hw)
    output["real_available"] = {
        'per': fPerc(mem_phys, mem_hw),
        'num': fBytes(mem_phys),
    }
    output["real_managed"] = {
        'per': fPerc(mem_all, mem_phys),
        'num': fBytes(mem_all),
    }
    output["logical_total"] = fBytes(mem_total)
    output["logical_used"] = {
        'per': fPerc(mem_used, mem_total),
        'num': fBytes(mem_used),
    }
    output["logical_free"] = {
        'per': fPerc(mem_avail, mem_total),
        'num': fBytes(mem_avail),
    }

    swap_total = Kstat["vm.swap_total"]
    output["swap_total"] = fBytes(swap_total)
    output["swap_reserved"] = fBytes(Kstat["vm.swap_reserved"])

    if int(swap_total) > 0:
        proc = Popen(
            "/usr/sbin/swapinfo -k|tail -1|awk '{print $3}'",
            shell=True,
            stdout=PIPE,
            stderr=PIPE,
        )
        try:
            swap_used = int(proc.communicate()[0])
        except:
            swap_used = 0
        output['swap_used'] = fBytes(swap_used * 1024)
    else:
        output['swap_used'] = fBytes(0)

    output['kmem_map_size'] = Kstat["vm.kmem_map_size"]
    output['kmem_map_free'] = Kstat["vm.kmem_map_free"]

    return output


def _system_memory(Kstat):

    arc = get_system_memory(Kstat)

    sys.stdout.write("System Memory:\n")
    sys.stdout.write("\n")
    sys.stdout.write("\t%s\t%s Active,\t" % (
        arc['active']['per'],
        arc['active']['num'],
        )
    )

    sys.stdout.write("%s\t%s Inact\n" % (
        arc['inact']['per'],
        arc['inact']['num'],
        )
    )
    sys.stdout.write("\t%s\t%s Wired,\t" % (
        arc['wired']['per'],
        arc['wired']['num'],
        )
    )
    sys.stdout.write("%s\t%s Cache\n" % (
        arc['cache']['per'],
        arc['cache']['num'],
        )
    )
    sys.stdout.write("\t%s\t%s Free,\t" % (
        arc['free']['per'],
        arc['free']['num'],
        )
    )
    sys.stdout.write("%s\t%s Gap\n" % (
        arc['gap']['per'],
        arc['gap']['num'],
        )
    )
    sys.stdout.write("\n")
    sys.stdout.write("\tReal Installed:\t\t\t\t%s\n" % arc['real_installed'])
    sys.stdout.write("\tReal Available:\t\t\t%s\t%s\n" % (
        arc['real_available']['per'],
        arc['real_available']['num'],
        )
    )
    sys.stdout.write("\tReal Managed:\t\t\t%s\t%s\n" % (
        arc['real_managed']['per'],
        arc['real_managed']['num'],
        )
    )

    sys.stdout.write("\n")
    sys.stdout.write("\tLogical Total:\t\t\t\t%s\n" % arc['logical_total'])
    sys.stdout.write("\tLogical Used:\t\t\t%s\t%s\n" % (
        arc['logical_used']['per'],
        arc['logical_used']['num'],
        )
    )
    sys.stdout.write("\tLogical Free:\t\t\t%s\t%s\n" % (
        arc['logical_free']['per'],
        arc['logical_free']['num'],
        )
    )
    sys.stdout.write("\n")

    cmd1 = """
        /sbin/kldstat | \
        /usr/bin/awk '
            BEGIN {
                print "16i 0";
            }
            NR > 1 {
                print toupper($4) "+";
            }
           END {
                print "p";
           }
        ' | /usr/bin/dc
    """

    cmd2 = """
        /usr/bin/vmstat -m | \
        /usr/bin/sed -Ee '1s/.*/0/;s/.* ([0-9]+)K.*/\\1+/;$s/$/1024*p/' | \
        /usr/bin/dc
    """

    p1 = Popen(cmd1, stdin=PIPE, stdout=PIPE, stderr=PIPE, shell=True, close_fds=True)
    p2 = Popen(cmd2, stdin=PIPE, stdout=PIPE, stderr=PIPE, shell=True, close_fds=True)

    ktext = D(p1.communicate()[0].strip())
    kdata = D(p2.communicate()[0].strip())

    if p1.returncode != 0 or p2.returncode != 0:
        sys.exit(1)

    kmem = ktext + kdata
    kmem_map_size = arc['kmem_map_size']
    kmem_map_free = arc['kmem_map_free']
    kmem_map_total = kmem_map_size + kmem_map_free

    sys.stdout.write("Kernel Memory:\t\t\t\t\t%s\n" % fBytes(kmem))
    sys.stdout.write("\tData:\t\t\t\t%s\t%s\n" % (fPerc(kdata, kmem), fBytes(kdata)))
    sys.stdout.write("\tText:\t\t\t\t%s\t%s\n\n" % (fPerc(ktext, kmem), fBytes(ktext)))

    sys.stdout.write("Kernel Memory Map:\t\t\t\t%s\n" % fBytes(kmem_map_total))
    sys.stdout.write("\tSize:\t\t\t\t%s\t%s\n" % (fPerc(kmem_map_size, kmem_map_total), fBytes(kmem_map_size)))
    sys.stdout.write("\tFree:\t\t\t\t%s\t%s\n" % (fPerc(kmem_map_free, kmem_map_total), fBytes(kmem_map_free)))


def get_arc_summary(Kstat):

    output = {}
    #if "vfs.zfs.version.spa" not in Kstat:
    #    return {}

    #spa = Kstat["vfs.zfs.version.spa"]
    #zpl = Kstat["vfs.zfs.version.zpl"]
    memory_throttle_count = Kstat[
        "kstat.zfs.misc.arcstats.memory_throttle_count"
        ]

    if memory_throttle_count > 0:
        output['health'] = 'THROTTLED'
    else:
        output['health'] = 'HEALTHY'

    #output['storage_pool_ver'] = spa
    #output['filesystem_ver'] = zpl
    output['memory_throttle_count'] = fHits(memory_throttle_count)

    ### ARC Misc. ###
    deleted = Kstat["kstat.zfs.misc.arcstats.deleted"]
    #evict_skip = Kstat["kstat.zfs.misc.arcstats.evict_skip"]
    mutex_miss = Kstat["kstat.zfs.misc.arcstats.mutex_miss"]
    recycle_miss = Kstat["kstat.zfs.misc.arcstats.recycle_miss"]

    ### ARC Misc. ###
    output["arc_misc"] = {}
    output["arc_misc"]["deleted"] = fHits(deleted)
    output["arc_misc"]['recycle_miss'] = fHits(recycle_miss)
    output["arc_misc"]['mutex_miss'] = fHits(mutex_miss)
    output["arc_misc"]['evict_skips'] = fHits(mutex_miss)

    ### ARC Sizing ###
    arc_size = Kstat["kstat.zfs.misc.arcstats.size"]
    mru_size = Kstat["kstat.zfs.misc.arcstats.p"]
    target_max_size = Kstat["kstat.zfs.misc.arcstats.c_max"]
    target_min_size = Kstat["kstat.zfs.misc.arcstats.c_min"]
    target_size = Kstat["kstat.zfs.misc.arcstats.c"]

    target_size_ratio = (target_max_size / target_min_size)

    ### ARC Sizing ###
    output['arc_sizing'] = {}
    output['arc_sizing']['arc_size'] = {
        'per': fPerc(arc_size, target_max_size),
        'num': fBytes(arc_size),
    }
    output['arc_sizing']['target_max_size'] = {
        'ratio': target_size_ratio,
        'num': fBytes(target_max_size),
    }
    output['arc_sizing']['target_min_size'] = {
        'per': fPerc(target_min_size, target_max_size),
        'num': fBytes(target_min_size),
    }
    output['arc_sizing']['target_size'] = {
        'per': fPerc(target_size, target_max_size),
        'num': fBytes(target_size),
    }

    ### ARC Hash Breakdown ###
    output['arc_hash_break'] = {}
    output['arc_hash_break']['hash_chain_max'] = Kstat[
        "kstat.zfs.misc.arcstats.hash_chain_max"
        ]
    output['arc_hash_break']['hash_chains'] = Kstat[
        "kstat.zfs.misc.arcstats.hash_chains"
        ]
    output['arc_hash_break']['hash_collisions'] = Kstat[
        "kstat.zfs.misc.arcstats.hash_collisions"
        ]
    output['arc_hash_break']['hash_elements'] = Kstat[
        "kstat.zfs.misc.arcstats.hash_elements"
        ]
    output['arc_hash_break']['hash_elements_max'] = Kstat[
        "kstat.zfs.misc.arcstats.hash_elements_max"
        ]

    output['arc_size_break'] = {}
    if arc_size > target_size:
        mfu_size = (arc_size - mru_size)
        output['arc_size_break']['recently_used_cache_size'] = {
            'per': fPerc(mru_size, arc_size),
            'num': fBytes(mru_size),
        }
        output['arc_size_break']['frequently_used_cache_size'] = {
            'per': fPerc(mfu_size, arc_size),
            'num': fBytes(mfu_size),
        }

    elif arc_size < target_size:
        mfu_size = (target_size - mru_size)
        output['arc_size_break']['recently_used_cache_size'] = {
            'per': fPerc(mru_size, target_size),
            'num': fBytes(mru_size),
        }
        output['arc_size_break']['frequently_used_cache_size'] = {
            'per': fPerc(mfu_size, target_size),
            'num': fBytes(mfu_size),
        }

    ### ARC Hash Breakdown ###
    hash_chain_max = Kstat["kstat.zfs.misc.arcstats.hash_chain_max"]
    hash_chains = Kstat["kstat.zfs.misc.arcstats.hash_chains"]
    hash_collisions = Kstat["kstat.zfs.misc.arcstats.hash_collisions"]
    hash_elements = Kstat["kstat.zfs.misc.arcstats.hash_elements"]
    hash_elements_max = Kstat["kstat.zfs.misc.arcstats.hash_elements_max"]

    output['arc_hash_break'] = {}
    output['arc_hash_break']['elements_max'] = fHits(hash_elements_max)
    output['arc_hash_break']['elements_current'] = {
        'per': fPerc(hash_elements, hash_elements_max),
        'num': fHits(hash_elements),
        }
    output['arc_hash_break']['collisions'] = fHits(hash_collisions)
    output['arc_hash_break']['chain_max'] = fHits(hash_chain_max)
    output['arc_hash_break']['chains'] = fHits(hash_chains)

    return output


def _arc_summary(Kstat):
    ### ARC Sizing ###
    arc = get_arc_summary(Kstat)

    sys.stdout.write("ARC Summary: (%s)\n" % arc['health'])

    #sys.stdout.write("\tStorage pool Version:\t\t\t%d\n" % arc['storage_pool_ver'])
    #sys.stdout.write("\tFilesystem Version:\t\t\t%d\n" % arc['filesystem_ver'])
    sys.stdout.write("\tMemory Throttle Count:\t\t\t%s\n" % arc['memory_throttle_count'])
    sys.stdout.write("\n")

    ### ARC Misc. ###
    sys.stdout.write("ARC Misc:\n")
    sys.stdout.write("\tDeleted:\t\t\t\t%s\n" % arc['arc_misc']['deleted'])
    sys.stdout.write("\tRecycle Misses:\t\t\t\t%s\n" % arc['arc_misc']['recycle_miss'])
    sys.stdout.write("\tMutex Misses:\t\t\t\t%s\n" % arc['arc_misc']['mutex_miss'])
    sys.stdout.write("\tEvict Skips:\t\t\t\t%s\n" % arc['arc_misc']['mutex_miss'])
    sys.stdout.write("\n")

    ### ARC Sizing ###
    sys.stdout.write("ARC Size:\t\t\t\t%s\t%s\n" % (
        arc['arc_sizing']['arc_size']['per'],
        arc['arc_sizing']['arc_size']['num']
        )
    )
    sys.stdout.write("\tTarget Size: (Adaptive)\t\t%s\t%s\n" % (
        arc['arc_sizing']['target_size']['per'],
        arc['arc_sizing']['target_size']['num'],
        )
    )

    sys.stdout.write("\tMin Size (Hard Limit):\t\t%s\t%s\n" % (
        arc['arc_sizing']['target_min_size']['per'],
        arc['arc_sizing']['target_min_size']['num'],
        )
    )

    sys.stdout.write("\tMax Size (High Water):\t\t%d:1\t%s\n" % (
        arc['arc_sizing']['target_max_size']['ratio'],
        arc['arc_sizing']['target_max_size']['num'],
        )
    )

    sys.stdout.write("\nARC Size Breakdown:\n")
    sys.stdout.write("\tRecently Used Cache Size:\t%s\t%s\n" % (
        arc['arc_size_break']['recently_used_cache_size']['per'],
        arc['arc_size_break']['recently_used_cache_size']['num'],
        )
    )
    sys.stdout.write("\tFrequently Used Cache Size:\t%s\t%s\n" % (
        arc['arc_size_break']['frequently_used_cache_size']['per'],
        arc['arc_size_break']['frequently_used_cache_size']['num'],
        )
    )

    sys.stdout.write("\n")

    ### ARC Hash Breakdown ###
    sys.stdout.write("ARC Hash Breakdown:\n")
    sys.stdout.write("\tElements Max:\t\t\t\t%s\n" % arc['arc_hash_break']['elements_max'])
    sys.stdout.write("\tElements Current:\t\t%s\t%s\n" % (
        arc['arc_hash_break']['elements_current']['per'],
        arc['arc_hash_break']['elements_current']['num'],
        )
    )
    sys.stdout.write("\tCollisions:\t\t\t\t%s\n" % arc['arc_hash_break']['collisions'])
    sys.stdout.write("\tChain Max:\t\t\t\t%s\n" % arc['arc_hash_break']['chain_max'])
    sys.stdout.write("\tChains:\t\t\t\t\t%s\n" % arc['arc_hash_break']['chains'])


def get_arc_efficiency(Kstat):
    output = {}

    #if "vfs.zfs.version.spa" not in Kstat:
    #    return

    arc_hits = Kstat["kstat.zfs.misc.arcstats.hits"]
    arc_misses = Kstat["kstat.zfs.misc.arcstats.misses"]
    demand_data_hits = Kstat["kstat.zfs.misc.arcstats.demand_data_hits"]
    demand_data_misses = Kstat["kstat.zfs.misc.arcstats.demand_data_misses"]
    demand_metadata_hits = Kstat[
        "kstat.zfs.misc.arcstats.demand_metadata_hits"
        ]
    demand_metadata_misses = Kstat[
        "kstat.zfs.misc.arcstats.demand_metadata_misses"
        ]
    mfu_ghost_hits = Kstat["kstat.zfs.misc.arcstats.mfu_ghost_hits"]
    mfu_hits = Kstat["kstat.zfs.misc.arcstats.mfu_hits"]
    mru_ghost_hits = Kstat["kstat.zfs.misc.arcstats.mru_ghost_hits"]
    mru_hits = Kstat["kstat.zfs.misc.arcstats.mru_hits"]
    prefetch_data_hits = Kstat["kstat.zfs.misc.arcstats.prefetch_data_hits"]
    prefetch_data_misses = Kstat[
        "kstat.zfs.misc.arcstats.prefetch_data_misses"
        ]
    prefetch_metadata_hits = Kstat[
        "kstat.zfs.misc.arcstats.prefetch_metadata_hits"
        ]
    prefetch_metadata_misses = Kstat[
        "kstat.zfs.misc.arcstats.prefetch_metadata_misses"
        ]

    anon_hits = arc_hits - (
        mfu_hits + mru_hits + mfu_ghost_hits + mru_ghost_hits
        )
    arc_accesses_total = (arc_hits + arc_misses)
    demand_data_total = (demand_data_hits + demand_data_misses)
    prefetch_data_total = (prefetch_data_hits + prefetch_data_misses)
    real_hits = (mfu_hits + mru_hits)

    output["total_accesses"] = fHits(arc_accesses_total)
    output["cache_hit_ratio"] = {
        'per': fPerc(arc_hits, arc_accesses_total),
        'num': fHits(arc_hits),
    }
    output["cache_miss_ratio"] = {
        'per': fPerc(arc_misses, arc_accesses_total),
        'num': fHits(arc_misses),
    }
    output["actual_hit_ratio"] = {
        'per': fPerc(real_hits, arc_accesses_total),
        'num': fHits(real_hits),
    }
    output["data_demand_efficiency"] = {
        'per': fPerc(demand_data_hits, demand_data_total),
        'num': fHits(demand_data_total),
    }

    if prefetch_data_total > 0:
        output["data_prefetch_efficiency"] = {
            'per': fPerc(prefetch_data_hits, prefetch_data_total),
            'num': fHits(prefetch_data_total),
        }

    if anon_hits > 0:
        output["cache_hits_by_cache_list"] = {}
        output["cache_hits_by_cache_list"]["anonymously_used"] = {
            'per': fPerc(anon_hits, arc_hits),
            'num': fHits(anon_hits),
        }

    output["most_recently_used"] = {
        'per': fPerc(mru_hits, arc_hits),
        'num': fHits(mru_hits),
    }
    output["most_frequently_used"] = {
        'per': fPerc(mfu_hits, arc_hits),
        'num': fHits(mfu_hits),
    }
    output["most_recently_used_ghost"] = {
        'per': fPerc(mru_ghost_hits, arc_hits),
        'num': fHits(mru_ghost_hits),
    }
    output["most_frequently_used_ghost"] = {
        'per': fPerc(mfu_ghost_hits, arc_hits),
        'num': fHits(mfu_ghost_hits),
    }

    output["cache_hits_by_data_type"] = {}
    output["cache_hits_by_data_type"]["demand_data"] = {
        'per': fPerc(demand_data_hits, arc_hits),
        'num': fHits(demand_data_hits),
    }
    output["cache_hits_by_data_type"]["prefetch_data"] = {
        'per': fPerc(prefetch_data_hits, arc_hits),
        'num': fHits(prefetch_data_hits),
    }
    output["cache_hits_by_data_type"]["demand_metadata"] = {
        'per': fPerc(demand_metadata_hits, arc_hits),
        'num': fHits(demand_metadata_hits),
    }
    output["cache_hits_by_data_type"]["prefetch_metadata"] = {
        'per': fPerc(prefetch_metadata_hits, arc_hits),
        'num': fHits(prefetch_metadata_hits),
    }

    output["cache_misses_by_data_type"] = {}
    output["cache_misses_by_data_type"]["demand_data"] = {
        'per': fPerc(demand_data_misses, arc_misses),
        'num': fHits(demand_data_misses),
    }
    output["cache_misses_by_data_type"]["prefetch_data"] = {
        'per': fPerc(prefetch_data_misses, arc_misses),
        'num': fHits(prefetch_data_misses),
    }
    output["cache_misses_by_data_type"]["demand_metadata"] = {
        'per': fPerc(demand_metadata_misses, arc_misses),
        'num': fHits(demand_metadata_misses),
    }
    output["cache_misses_by_data_type"]["prefetch_metadata"] = {
        'per': fPerc(prefetch_metadata_misses, arc_misses),
        'num': fHits(prefetch_metadata_misses),
    }

    return output


def _arc_efficiency(Kstat):
    arc = get_arc_efficiency(Kstat)

    sys.stdout.write("ARC Total accesses:\t\t\t\t\t%s\n" % arc['total_accesses'])
    sys.stdout.write("\tCache Hit Ratio:\t\t%s\t%s\n" % (
        arc['cache_hit_ratio']['per'],
        arc['cache_hit_ratio']['num'],
        )
    )
    sys.stdout.write("\tCache Miss Ratio:\t\t%s\t%s\n" % (
        arc['cache_miss_ratio']['per'],
        arc['cache_miss_ratio']['num'],
        )
    )

    sys.stdout.write("\tActual Hit Ratio:\t\t%s\t%s\n" % (
        arc['actual_hit_ratio']['per'],
        arc['actual_hit_ratio']['num'],
        )
    )

    sys.stdout.write("\n")
    sys.stdout.write("\tData Demand Efficiency:\t\t%s\t%s\n" % (
        arc['data_demand_efficiency']['per'],
        arc['data_demand_efficiency']['num'],
        )
    )

    if 'data_prefetch_efficiency' in arc:
        sys.stdout.write("\tData Prefetch Efficiency:\t%s\t%s\n" % (
            arc['data_prefetch_efficiency']['per'],
            arc['data_prefetch_efficiency']['num'],
            )
        )
    sys.stdout.write("\n")

    sys.stdout.write("\tCACHE HITS BY CACHE LIST:\n")
    if 'cache_hits_by_cache_list' in arc:
        sys.stdout.write("\t  Anonymously Used:\t\t%s\t%s\n" % (
            arc['cache_hits_by_cache_list']['anonymously_used']['per'],
            arc['cache_hits_by_cache_list']['anonymously_used']['num'],
            )
        )
    sys.stdout.write("\t  Most Recently Used:\t\t%s\t%s\n" % (
        arc['most_recently_used']['per'],
        arc['most_recently_used']['num'],
        )
    )
    sys.stdout.write("\t  Most Frequently Used:\t\t%s\t%s\n" % (
        arc['most_frequently_used']['per'],
        arc['most_frequently_used']['num'],
        )
    )
    sys.stdout.write("\t  Most Recently Used Ghost:\t%s\t%s\n" % (
        arc['most_recently_used_ghost']['per'],
        arc['most_recently_used_ghost']['num'],
        )
    )
    sys.stdout.write("\t  Most Frequently Used Ghost:\t%s\t%s\n" % (
        arc['most_frequently_used_ghost']['per'],
        arc['most_frequently_used_ghost']['num'],
        )
    )

    sys.stdout.write("\n\tCACHE HITS BY DATA TYPE:\n")
    sys.stdout.write("\t  Demand Data:\t\t\t%s\t%s\n" % (
        arc["cache_hits_by_data_type"]['demand_data']['per'],
        arc["cache_hits_by_data_type"]['demand_data']['num'],
        )
    )
    sys.stdout.write("\t  Prefetch Data:\t\t%s\t%s\n" % (
        arc["cache_hits_by_data_type"]['prefetch_data']['per'],
        arc["cache_hits_by_data_type"]['prefetch_data']['num'],
        )
    )
    sys.stdout.write("\t  Demand Metadata:\t\t%s\t%s\n" % (
        arc["cache_hits_by_data_type"]['demand_metadata']['per'],
        arc["cache_hits_by_data_type"]['demand_metadata']['num'],
        )
    )
    sys.stdout.write("\t  Prefetch Metadata:\t\t%s\t%s\n" % (
        arc["cache_hits_by_data_type"]['prefetch_metadata']['per'],
        arc["cache_hits_by_data_type"]['prefetch_metadata']['num'],
        )
    )

    sys.stdout.write("\n\tCACHE MISSES BY DATA TYPE:\n")
    sys.stdout.write("\t  Demand Data:\t\t\t%s\t%s\n" % (
        arc["cache_misses_by_data_type"]['demand_data']['per'],
        arc["cache_misses_by_data_type"]['demand_data']['num'],
        )
    )
    sys.stdout.write("\t  Prefetch Data:\t\t%s\t%s\n" % (
        arc["cache_misses_by_data_type"]['prefetch_data']['per'],
        arc["cache_misses_by_data_type"]['prefetch_data']['num'],
        )
    )
    sys.stdout.write("\t  Demand Metadata:\t\t%s\t%s\n" % (
        arc["cache_misses_by_data_type"]['demand_metadata']['per'],
        arc["cache_misses_by_data_type"]['demand_metadata']['num'],
        )
    )
    sys.stdout.write("\t  Prefetch Metadata:\t\t%s\t%s\n" % (
        arc["cache_misses_by_data_type"]['prefetch_metadata']['per'],
        arc["cache_misses_by_data_type"]['prefetch_metadata']['num'],
        )
    )


def get_l2arc_summary(Kstat):
    output = {}

    #if "vfs.zfs.version.spa" not in Kstat:
    #    return

    l2_abort_lowmem = Kstat["kstat.zfs.misc.arcstats.l2_abort_lowmem"]
    l2_cksum_bad = Kstat["kstat.zfs.misc.arcstats.l2_cksum_bad"]
    l2_evict_lock_retry = Kstat["kstat.zfs.misc.arcstats.l2_evict_lock_retry"]
    l2_evict_reading = Kstat["kstat.zfs.misc.arcstats.l2_evict_reading"]
    l2_feeds = Kstat["kstat.zfs.misc.arcstats.l2_feeds"]
    l2_free_on_write = Kstat["kstat.zfs.misc.arcstats.l2_free_on_write"]
    l2_hdr_size = Kstat["kstat.zfs.misc.arcstats.l2_hdr_size"]
    l2_hits = Kstat["kstat.zfs.misc.arcstats.l2_hits"]
    l2_io_error = Kstat["kstat.zfs.misc.arcstats.l2_io_error"]
    l2_misses = Kstat["kstat.zfs.misc.arcstats.l2_misses"]
    l2_rw_clash = Kstat["kstat.zfs.misc.arcstats.l2_rw_clash"]
    l2_size = Kstat["kstat.zfs.misc.arcstats.l2_size"]
    l2_asize = Kstat["kstat.zfs.misc.arcstats.l2_asize"]
    #l2_write_buffer_bytes_scanned = Kstat["kstat.zfs.misc.arcstats.l2_write_buffer_bytes_scanned"] # not in ZoL?
    #l2_write_buffer_iter = Kstat["kstat.zfs.misc.arcstats.l2_write_buffer_iter"] # not in ZoL?
    #l2_write_buffer_list_iter = Kstat["kstat.zfs.misc.arcstats.l2_write_buffer_list_iter"] # not in ZoL?
    #l2_write_buffer_list_null_iter = Kstat["kstat.zfs.misc.arcstats.l2_write_buffer_list_null_iter"] # not in ZoL?
    #l2_write_bytes = Kstat["kstat.zfs.misc.arcstats.l2_write_bytes"]
    #l2_write_full = Kstat["kstat.zfs.misc.arcstats.l2_write_full"]  # not in ZoL?
    #l2_write_in_l2 = Kstat["kstat.zfs.misc.arcstats.l2_write_in_l2"]
    #l2_write_io_in_progress = Kstat["kstat.zfs.misc.arcstats.l2_write_io_in_progress"] # not in ZoL?
    #l2_write_not_cacheable = Kstat["kstat.zfs.misc.arcstats.l2_write_not_cacheable"]
    #l2_write_passed_headroom = Kstat["kstat.zfs.misc.arcstats.l2_write_passed_headroom"]  # not in ZoL?
    #l2_write_pios = Kstat["kstat.zfs.misc.arcstats.l2_write_pios"]
    #l2_write_spa_mismatch = Kstat["kstat.zfs.misc.arcstats.l2_write_spa_mismatch"]   # not in ZoL?
    #l2_write_trylock_fail = Kstat["kstat.zfs.misc.arcstats.l2_write_trylock_fail"]  # not in ZoL?
    l2_writes_done = Kstat["kstat.zfs.misc.arcstats.l2_writes_done"]
    l2_writes_error = Kstat["kstat.zfs.misc.arcstats.l2_writes_error"]
    #l2_writes_hdr_miss = Kstat["kstat.zfs.misc.arcstats.l2_writes_hdr_miss"]
    l2_writes_sent = Kstat["kstat.zfs.misc.arcstats.l2_writes_sent"]

    l2_access_total = (l2_hits + l2_misses)
    output['l2_health_count'] = (l2_writes_error + l2_cksum_bad + l2_io_error)

    output['l2_access_total'] = l2_access_total
    output['l2_size'] = l2_size
    output['l2_asize'] = l2_asize

    if l2_size > 0 and l2_access_total > 0:

        if output['l2_health_count'] > 0:
            output["health"] = "DEGRADED"
        else:
            output["health"] = "HEALTHY"

        #output["passed_headroom"] = fHits(l2_write_passed_headroom)
        #output["tried_lock_failure"] = fHits(l2_write_trylock_fail)
        #output["io_in_progress"] = fHits(l2_write_io_in_progress)
        output["low_memory_aborts"] = fHits(l2_abort_lowmem)
        output["free_on_write"] = fHits(l2_free_on_write)
        #output["writes_while_full"] = fHits(l2_write_full)
        output["rw_clashes"] = fHits(l2_rw_clash)
        output["bad_checksums"] = fHits(l2_cksum_bad)
        output["io_errors"] = fHits(l2_io_error)
        #output["spa_mismatch"] = fHits(l2_write_spa_mismatch)

        output["l2_arc_size"] = {}
        output["l2_arc_size"]["adative"] = fBytes(l2_size)
        output["l2_arc_size"]["actual"] = {
            'per': fPerc(l2_asize, l2_size),
            'num': fBytes(l2_asize)
            }
        output["l2_arc_size"]["head_size"] = {
            'per': fPerc(l2_hdr_size, l2_size),
            'num': fBytes(l2_hdr_size),
        }

        output["l2_arc_evicts"] = {}
        output["l2_arc_evicts"]['lock_retries'] = fHits(l2_evict_lock_retry)
        output["l2_arc_evicts"]['reading'] = fHits(l2_evict_reading)

        output['l2_arc_breakdown'] = {}
        output['l2_arc_breakdown']['value'] = fHits(l2_access_total)
        output['l2_arc_breakdown']['hit_ratio'] = {
            'per': fPerc(l2_hits, l2_access_total),
            'num': fHits(l2_hits),
        }
        output['l2_arc_breakdown']['miss_ratio'] = {
            'per': fPerc(l2_misses, l2_access_total),
            'num': fHits(l2_misses),
        }
        output['l2_arc_breakdown']['feeds'] = fHits(l2_feeds)

        output['l2_arc_buffer'] = {}
        #output['l2_arc_buffer']['bytes_scanned'] = fBytes(l2_write_buffer_bytes_scanned)
        #output['l2_arc_buffer']['buffer_iterations'] = fHits(l2_write_buffer_iter)
        #output['l2_arc_buffer']['list_iterations'] = fHits(l2_write_buffer_list_iter)
        #output['l2_arc_buffer']['null_list_iterations'] = fHits(l2_write_buffer_list_null_iter)

        output['l2_arc_writes'] = {}
        output['l2_writes_done'] = l2_writes_done
        output['l2_writes_sent'] = l2_writes_sent
        if l2_writes_done != l2_writes_sent:
            output['l2_arc_writes']['writes_sent'] = {
                'value': "FAULTED",
                'num': fHits(l2_writes_sent),
            }
            output['l2_arc_writes']['done_ratio'] = {
                'per': fPerc(l2_writes_done, l2_writes_sent),
                'num': fHits(l2_writes_done),
            }
            output['l2_arc_writes']['error_ratio'] = {
                'per': fPerc(l2_writes_error, l2_writes_sent),
                'num': fHits(l2_writes_error),
            }
        else:
            output['l2_arc_writes']['writes_sent'] = {
                'per': fPerc(100),
                'num': fHits(l2_writes_sent),
            }

    return output


def _l2arc_summary(Kstat):

    #if not Kstat["vfs.zfs.version.spa"]:
    #    return

    arc = get_l2arc_summary(Kstat)

    if arc['l2_size'] > 0 and arc['l2_access_total'] > 0:
        sys.stdout.write("L2 ARC Summary: ")
        if arc['l2_health_count'] > 0:
            sys.stdout.write("(DEGRADED)\n")
        else:
            sys.stdout.write("(HEALTHY)\n")
        #sys.stdout.write("\tPassed Headroom:\t\t\t%s\n" % arc['passed_headroom'])
        #sys.stdout.write("\tTried Lock Failures:\t\t\t%s\n" % arc['tried_lock_failure'])
        #sys.stdout.write("\tIO In Progress:\t\t\t\t%s\n" % arc['io_in_progress'])
        sys.stdout.write("\tLow Memory Aborts:\t\t\t%s\n" % arc['low_memory_aborts'])
        sys.stdout.write("\tFree on Write:\t\t\t\t%s\n" % arc['free_on_write'])
        #sys.stdout.write("\tWrites While Full:\t\t\t%s\n" % arc['writes_while_full'])
        sys.stdout.write("\tR/W Clashes:\t\t\t\t%s\n" % arc['rw_clashes'])
        sys.stdout.write("\tBad Checksums:\t\t\t\t%s\n" % arc['bad_checksums'])
        sys.stdout.write("\tIO Errors:\t\t\t\t%s\n" % arc['io_errors'])
        #sys.stdout.write("\tSPA Mismatch:\t\t\t\t%s\n" % arc['spa_mismatch'])
        sys.stdout.write("\n")

        sys.stdout.write("L2 ARC Size: (Adaptive)\t\t\t\t%s\n" % arc["l2_arc_size"]["adative"])
        sys.stdout.write("\tCompressed:\t\t\t%s\t%s\n" % (
            arc["l2_arc_size"]["actual"]["per"],
            arc["l2_arc_size"]["actual"]["num"],
            )
        )
        sys.stdout.write("\tHeader Size:\t\t\t%s\t%s\n" % (
            arc["l2_arc_size"]["head_size"]["per"],
            arc["l2_arc_size"]["head_size"]["num"],
            )
        )
        sys.stdout.write("\n")

        if arc["l2_arc_evicts"]['lock_retries'] + arc["l2_arc_evicts"]["reading"] > 0:
            sys.stdout.write("L2 ARC Evicts:\n")
            sys.stdout.write("\tLock Retries:\t\t\t\t%s\n" % arc["l2_arc_evicts"]['lock_retries'])
            sys.stdout.write("\tUpon Reading:\t\t\t\t%s\n" % arc["l2_arc_evicts"]["reading"])
            sys.stdout.write("\n")

        sys.stdout.write("L2 ARC Breakdown:\t\t\t\t%s\n" % arc['l2_arc_breakdown']['value'])
        sys.stdout.write("\tHit Ratio:\t\t\t%s\t%s\n" % (
            arc['l2_arc_breakdown']['hit_ratio']['per'],
            arc['l2_arc_breakdown']['hit_ratio']['num'],
            )
        )

        sys.stdout.write("\tMiss Ratio:\t\t\t%s\t%s\n" % (
            arc['l2_arc_breakdown']['miss_ratio']['per'],
            arc['l2_arc_breakdown']['miss_ratio']['num'],
            )
        )

        sys.stdout.write("\tFeeds:\t\t\t\t\t%s\n" % arc['l2_arc_breakdown']['feeds'])
        sys.stdout.write("\n")

        #sys.stdout.write("L2 ARC Buffer:\n")
        #sys.stdout.write("\tBytes Scanned:\t\t\t\t%s\n" % arc['l2_arc_buffer']['bytes_scanned'])
        #sys.stdout.write("\tBuffer Iterations:\t\t\t%s\n" % arc['l2_arc_buffer']['buffer_iterations'])
        #sys.stdout.write("\tList Iterations:\t\t\t%s\n" % arc['l2_arc_buffer']['list_iterations'])
        #sys.stdout.write("\tNULL List Iterations:\t\t\t%s\n" % arc['l2_arc_buffer']['null_list_iterations'])
        #sys.stdout.write("\n")

        sys.stdout.write("L2 ARC Writes:\n")
        if arc['l2_writes_done'] != arc['l2_writes_sent']:
            sys.stdout.write("\tWrites Sent: (%s)\t\t\t\t%s\n" % (
                arc['l2_arc_writes']['writes_sent']['value'],
                arc['l2_arc_writes']['writes_sent']['num'],
                )
            )
            sys.stdout.write("\t  Done Ratio:\t\t\t%s\t%s\n" % (
                arc['l2_arc_writes']['done_ratio']['per'],
                arc['l2_arc_writes']['done_ratio']['num'],
                )
            )
            sys.stdout.write("\t  Error Ratio:\t\t\t%s\t%s\n" % (
                arc['l2_arc_writes']['error_ratio']['per'],
                arc['l2_arc_writes']['error_ratio']['num'],
                )
            )
        else:
            sys.stdout.write("\tWrites Sent:\t\t\t%s\t%s\n" % (
                arc['l2_arc_writes']['writes_sent']['per'],
                arc['l2_arc_writes']['writes_sent']['num'],
                )
            )


def get_dmu_summary(Kstat):
    output = {}

    #if "vfs.zfs.version.spa" not in Kstat:
    #    return output

    zfetch_bogus_streams = Kstat["kstat.zfs.misc.zfetchstats.bogus_streams"]
    zfetch_colinear_hits = Kstat["kstat.zfs.misc.zfetchstats.colinear_hits"]
    zfetch_colinear_misses = Kstat["kstat.zfs.misc.zfetchstats.colinear_misses"]
    zfetch_hits = Kstat["kstat.zfs.misc.zfetchstats.hits"]
    zfetch_misses = Kstat["kstat.zfs.misc.zfetchstats.misses"]
    zfetch_reclaim_failures = Kstat["kstat.zfs.misc.zfetchstats.reclaim_failures"]
    zfetch_reclaim_successes = Kstat["kstat.zfs.misc.zfetchstats.reclaim_successes"]
    zfetch_streams_noresets = Kstat["kstat.zfs.misc.zfetchstats.streams_noresets"]
    zfetch_streams_resets = Kstat["kstat.zfs.misc.zfetchstats.streams_resets"]
    zfetch_stride_hits = Kstat["kstat.zfs.misc.zfetchstats.stride_hits"]
    zfetch_stride_misses = Kstat["kstat.zfs.misc.zfetchstats.stride_misses"]

    zfetch_access_total = (zfetch_hits + zfetch_misses)
    zfetch_colinear_total = (zfetch_colinear_hits + zfetch_colinear_misses)
    zfetch_health_count = (zfetch_bogus_streams)
    zfetch_reclaim_total = (zfetch_reclaim_successes + zfetch_reclaim_failures)
    zfetch_streams_total = (zfetch_streams_resets + zfetch_streams_noresets + zfetch_bogus_streams)
    zfetch_stride_total = (zfetch_stride_hits + zfetch_stride_misses)
    output['zfetch_access_total'] = zfetch_access_total

    if zfetch_access_total > 0:

        output['file_level_prefetch'] = {}
        if zfetch_health_count > 0:
            output['file_level_prefetch']['health'] = 'DEGRADED'
        else:
            output['file_level_prefetch']['health'] = 'HEALTHY'

        output['dmu'] = {}
        output['dmu']['efficiency'] = {}
        output['dmu']['efficiency']['value'] = fHits(zfetch_access_total)
        output['dmu']['efficiency']['hit_ratio'] = {
            'per': fPerc(zfetch_hits, zfetch_access_total),
            'num': fHits(zfetch_hits),
        }
        output['dmu']['efficiency']['miss_ratio'] = {
            'per': fPerc(zfetch_misses, zfetch_access_total),
            'num': fHits(zfetch_misses),
        }

        output['dmu']['colinear'] = {}
        output['dmu']['colinear']['value'] = fHits(zfetch_colinear_total)
        output['dmu']['colinear']['hit_ratio'] = {
            'per': fPerc(zfetch_colinear_hits, zfetch_colinear_total),
            'num': fHits(zfetch_colinear_hits),
        }
        output['dmu']['colinear']['miss_ratio'] = {
            'per': fPerc(zfetch_colinear_misses, zfetch_colinear_total),
            'num': fHits(zfetch_colinear_misses),
        }

        output['dmu']['stride'] = {}
        output['dmu']['stride']['value'] = fHits(zfetch_stride_total)
        output['dmu']['stride']['hit_ratio'] = {
            'per': fPerc(zfetch_stride_hits, zfetch_stride_total),
            'num': fHits(zfetch_stride_hits),
        }
        output['dmu']['stride']['miss_ratio'] = {
            'per': fPerc(zfetch_stride_misses, zfetch_stride_total),
            'num': fHits(zfetch_stride_misses),
        }

        output['dmu_misc'] = {}
        if zfetch_health_count > 0:
            output['dmu_misc']['status'] = "FAULTED"
        else:
            output['dmu_misc']['status'] = ""

        output['dmu_misc']['reclaim'] = {}
        output['dmu_misc']['reclaim']['value'] = fHits(zfetch_reclaim_total)
        output['dmu_misc']['reclaim']['successes'] = {
            'per': fPerc(zfetch_reclaim_successes, zfetch_reclaim_total),
            'num': fHits(zfetch_reclaim_successes),
        }
        output['dmu_misc']['reclaim']['failure'] = {
            'per': fPerc(zfetch_reclaim_failures, zfetch_reclaim_total),
            'num': fHits(zfetch_reclaim_failures),
        }

        output['dmu_misc']['streams'] = {}
        output['dmu_misc']['streams']['value'] = fHits(zfetch_streams_total)
        output['dmu_misc']['streams']['plus_resets'] = {
            'per': fPerc(zfetch_streams_resets, zfetch_streams_total),
            'num': fHits(zfetch_streams_resets),
        }
        output['dmu_misc']['streams']['neg_resets'] = {
            'per': fPerc(zfetch_streams_noresets, zfetch_streams_total),
            'num': fHits(zfetch_streams_noresets),
        }
        output['dmu_misc']['streams']['bogus'] = fHits(zfetch_bogus_streams)

    return output


def _dmu_summary(Kstat):

    arc = get_dmu_summary(Kstat)

    if arc['zfetch_access_total'] > 0:
        sys.stdout.write("File-Level Prefetch: (%s)" % arc['file_level_prefetch']['health'])
        sys.stdout.write("\n")

        sys.stdout.write("DMU Efficiency:\t\t\t\t\t%s\n" % arc['dmu']['efficiency']['value'])
        sys.stdout.write("\tHit Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['efficiency']['hit_ratio']['per'],
            arc['dmu']['efficiency']['hit_ratio']['num'],
            )
        )
        sys.stdout.write("\tMiss Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['efficiency']['miss_ratio']['per'],
            arc['dmu']['efficiency']['miss_ratio']['num'],
            )
        )

        sys.stdout.write("\n")

        sys.stdout.write("\tColinear:\t\t\t\t%s\n" % arc['dmu']['colinear']['value'])
        sys.stdout.write("\t  Hit Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['colinear']['hit_ratio']['per'],
            arc['dmu']['colinear']['hit_ratio']['num'],
            )
        )

        sys.stdout.write("\t  Miss Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['colinear']['miss_ratio']['per'],
            arc['dmu']['colinear']['miss_ratio']['num'],
            )
        )

        sys.stdout.write("\n")

        sys.stdout.write("\tStride:\t\t\t\t\t%s\n" % arc['dmu']['stride']['value'])
        sys.stdout.write("\t  Hit Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['stride']['hit_ratio']['per'],
            arc['dmu']['stride']['hit_ratio']['num'],
            )
        )

        sys.stdout.write("\t  Miss Ratio:\t\t\t%s\t%s\n" % (
            arc['dmu']['stride']['miss_ratio']['per'],
            arc['dmu']['stride']['miss_ratio']['num'],
            )
        )

        sys.stdout.write("\n")
        sys.stdout.write("DMU Misc: %s\n" % arc['dmu_misc']['status'])

        sys.stdout.write("\tReclaim:\t\t\t\t%s\n" % arc['dmu_misc']['reclaim']['value'])
        sys.stdout.write("\t  Successes:\t\t\t%s\t%s\n" % (
            arc['dmu_misc']['reclaim']['successes']['per'],
            arc['dmu_misc']['reclaim']['successes']['num'],
            )
        )

        sys.stdout.write("\t  Failures:\t\t\t%s\t%s\n" % (
            arc['dmu_misc']['reclaim']['failure']['per'],
            arc['dmu_misc']['reclaim']['failure']['num'],
            )
        )

        sys.stdout.write("\n\tStreams:\t\t\t\t%s\n" % arc['dmu_misc']['streams']['value'])
        sys.stdout.write("\t  +Resets:\t\t\t%s\t%s\n" % (
            arc['dmu_misc']['streams']['plus_resets']['per'],
            arc['dmu_misc']['streams']['plus_resets']['num'],
            )
        )

        sys.stdout.write("\t  -Resets:\t\t\t%s\t%s\n" % (
            arc['dmu_misc']['streams']['neg_resets']['per'],
            arc['dmu_misc']['streams']['neg_resets']['num'],
            )
        )

        sys.stdout.write("\t  Bogus:\t\t\t\t%s\n" % arc['dmu_misc']['streams']['bogus'])


def get_vdev_summary(Kstat):
    output = {}

    #if "vfs.zfs.version.spa" not in Kstat:
    #    return

    vdev_cache_delegations = Kstat["kstat.zfs.misc.vdev_cache_stats.delegations"]
    vdev_cache_misses = Kstat["kstat.zfs.misc.vdev_cache_stats.misses"]
    vdev_cache_hits = Kstat["kstat.zfs.misc.vdev_cache_stats.hits"]
    vdev_cache_total = (vdev_cache_misses + vdev_cache_hits + vdev_cache_delegations)

    output['vdev_cache_total'] = vdev_cache_total

    if vdev_cache_total > 0:
        output['summary'] = fHits(vdev_cache_total)
        output['hit_ratio'] = {
            'per': fPerc(vdev_cache_hits, vdev_cache_total),
            'num': fHits(vdev_cache_hits),
        }
        output['miss_ratio'] = {
            'per': fPerc(vdev_cache_misses, vdev_cache_total),
            'num': fHits(vdev_cache_misses),
        }
        output['delegations'] = {
            'per': fPerc(vdev_cache_delegations, vdev_cache_total),
            'num': fHits(vdev_cache_delegations),
        }

    return output


def _vdev_summary(Kstat):
    arc = get_vdev_summary(Kstat)

    if arc['vdev_cache_total'] > 0:
        sys.stdout.write("VDEV Cache Summary:\t\t\t\t%s\n" % arc['summary'])
        sys.stdout.write("\tHit Ratio:\t\t\t%s\t%s\n" % (
            arc['hit_ratio']['per'],
            arc['hit_ratio']['num'],
        ))
        sys.stdout.write("\tMiss Ratio:\t\t\t%s\t%s\n" % (
            arc['miss_ratio']['per'],
            arc['miss_ratio']['num'],
        ))
        sys.stdout.write("\tDelegations:\t\t\t%s\t%s\n" % (
            arc['delegations']['per'],
            arc['delegations']['num'],
        ))


def get_systl_summary(Kstat):
    output = {}
    return output


def _sysctl_summary(Kstat):
    global show_sysctl_descriptions
    global alternate_sysctl_layout

    Tunable = [
        "kern.maxusers",
        "vm.kmem_size",
        "vm.kmem_size_scale",
        "vm.kmem_size_min",
        "vm.kmem_size_max",
        "vfs.zfs"
    ]

    if not usetunable:
        return

    sysctl_descriptions = {}
    if show_sysctl_descriptions:
        tunables = " ".join(str(x) for x in Tunable)
        p = Popen("/sbin/sysctl -qde %s" % tunables, stdin=PIPE,
            stdout=PIPE, stderr=PIPE, shell=True, close_fds=True)
        p.wait()

        descriptions = p.communicate()[0].split('\n')
        if p.returncode != 0:
            sys.exit(1)

        for tunable in descriptions:
            if not tunable:
                continue
            tunable = tunable.strip()
            name, description = tunable.split('=')[:2]
            name = name.strip()
            description = description.strip()
            if not description:
                description = "Description unavailable"
            sysctl_descriptions[name] = description

    tunables = " ".join(str(x) for x in Tunable)
    p = Popen("/sbin/sysctl -qe %s" % tunables, stdin=PIPE,
        stdout=PIPE, stderr=PIPE, shell=True, close_fds=True)
    p.wait()

    zfs_tunables = p.communicate()[0].split('\n')
    if p.returncode != 0:
        sys.exit(1)

    sys.stdout.write("ZFS Tunable (sysctl):\n")
    for tunable in zfs_tunables:
        if not tunable:
            continue
        tunable = tunable.strip()
        name, value = tunable.split("=")[0:2]
        name = name.strip()
        value = D(value.strip())
        format = "\t%s=%d\n" if alternate_sysctl_layout else "\t%-40s%d\n"
        if show_sysctl_descriptions:
            sys.stdout.write("\t\# %s\n" % sysctl_descriptions[name])
        sys.stdout.write(format % (name, value))


unSub = [
    #_system_memory,
    _arc_summary,
    _arc_efficiency,
    _l2arc_summary,
    _dmu_summary,
    _vdev_summary,
    #_sysctl_summary
]


def _call_all():
    page = 1
    Kstat = get_Kstat()
    for unsub in unSub:
        unsub(Kstat)
        sys.stdout.write("\t\t\t\t\t\t\t\tPage: %2d" % page)
        div2()
        page += 1


def zfs_header():
    daydate = time.strftime("%a %b %d %H:%M:%S %Y")

    div1()
    sys.stdout.write("ZFS Subsystem Report\t\t\t\t%s" % daydate)
    div2()


def main():
    global show_sysctl_descriptions
    global alternate_sysctl_layout

    opts, args = getopt.getopt(
        sys.argv[1:], "adp:"
    )

    args = {}
    for opt, arg in opts:
        if opt == '-a':
            args['a'] = True
        if opt == '-d':
            args['d'] = True
        if opt == '-p':
            args['p'] = arg

    if args:
        alternate_sysctl_layout = True if 'a' in args else False
        show_sysctl_descriptions = True if 'd' in args else False
        try:
            zfs_header()
            unSub[int(args['p']) - 1]()
            div2()

        except:
            _call_all()

    else:
        _call_all()


if __name__ == '__main__':
    main()
