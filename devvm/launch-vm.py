#!/usr/bin/env python3

import subprocess
import os
from pathlib import Path
import argparse
import itertools
import re

parser = argparse.ArgumentParser()
parser.add_argument("--hdd-qcow2-image", type=Path, required=True)
parser.add_argument("--name", type=str, required=True)
parser.add_argument("--bridge", type=str, required=True)
parser.add_argument("-m", type=str, required=True)
parser.add_argument("--smp", type=str, required=True, help="example: " "8,sockets=1,cores=8,threads=1",
)
parser.add_argument("--vfio-passthrough", type=str, action='append', help="like 0000:3c:00.0", default=[])
parser.add_argument("--undo-vfio-passthrough", type=str)
parser.add_argument("--nvdimm", type=str, action='append', default=[])
args = parser.parse_args()

hdd_img = args.hdd_qcow2_image
assert hdd_img.is_file()
assert "qcow2" in hdd_img.name

bridgepath = Path("/sys/class/net/") / args.bridge
assert bridgepath.exists()

import collections
VFIODev = collections.namedtuple('VFIODev', ['domain_bus_slot_function', 'bus_slot_function', 'sysfs'])
vfiodevs = []
for domain_bus_slot_function in args.vfio_passthrough:
    p = Path("/sys/bus/pci/devices") / domain_bus_slot_function
    assert p.exists()
    if not domain_bus_slot_function.startswith("0000:"):
        raise f"qemu -device vfio-pci,host=$bus:$slot:$function assumes $domain == 0000, got {domain_bus_slot_function}"
    else:
        bus_slot_function = domain_bus_slot_function[5:]
    vfiodevs.append(VFIODev(domain_bus_slot_function=domain_bus_slot_function, bus_slot_function=bus_slot_function, sysfs=p))

def mustremoveprefix(s, prefix):
    if not s.startswith(prefix):
        raise f"{s!r} should have prefix {prefix!r}"
    return s[len(prefix):]

NVDIMMBase = collections.namedtuple('NVDIMM', ['arg', 'path', 'size', 'pmem'])
class NVDIMM(NVDIMMBase):
    def qemu_args(self, slot):
        return [
            "-object",
            f"memory-backend-file,id=mem{slot},pmem={self.pmem},mem-path={self.path},size={self.size},align=2M,share=on",
            "-device",
            f"nvdimm,id=nvdimm{slot},memdev=mem{slot},slot={slot}"
        ]

nvdimms = []
for arg in args.nvdimm:
    split = arg.split(sep=',', maxsplit=3)
    assert len(split) == 3
    path = Path(split[0])
    assert path.exists()
    size = mustremoveprefix(split[1], "size=")
    pmem = mustremoveprefix(split[2], "pmem=")
    assert pmem in [ "on", "off"]
    nvdimms.append(NVDIMM(arg=arg, path=path, size=size, pmem=pmem))

this_path = Path(os.path.dirname(os.path.realpath(__file__)))
upscriptpath = this_path / "launch-vm.up.bash"
downscriptpath = this_path / "launch-vm.down.bash"

upscript = f"""
#!/usr/bin/env bash
set -euo pipefail

IFACE="$1"
BRIDGE={args.bridge}

ip link set dev "$IFACE" up
brctl addif "$BRIDGE" "$IFACE"
"""

downscript = f"""
#!/usr/bin/env bash
set -euo pipefail

IFACE="$1"
BRIDGE={args.bridge}

brctl delif "$BRIDGE" "$IFACE"
ip link set dev "$IFACE" down
"""

with open(upscriptpath, "w") as f:
    f.write(upscript.strip())

with open(downscriptpath, "w") as f:
    f.write(downscript.strip())

upscriptpath.chmod(0o700)
downscriptpath.chmod(0o700)

def pcie_get_currently_bound_driver(dev):
    r = re.compile(r"^\s+Kernel driver in use: (\S+)$")
    o = subprocess.run(["lspci", "-k", "-s", d.domain_bus_slot_function ], check=True, capture_output=True, text=True)
    for line in o.stdout.splitlines():
        m = r.match(line)
        if not m:
            continue
        return m.group(1)
    return None

for d in vfiodevs:
    modulename_to_drivername = {
        "vfio_pci":"vfio-pci",
        "nvme":"nvme",
    }
    if args.undo_vfio_passthrough is not None:
        modulename = args.undo_vfio_passthrough
    else:
        modulename = "vfio_pci"
    drivername = modulename_to_drivername[modulename]

    cd = pcie_get_currently_bound_driver(d)
    if cd == drivername:
        continue
    else:
        if cd is not None:
            unbind = d.sysfs / "driver" / "unbind"
            unbind.write_text(d.domain_bus_slot_function)
        cd = pcie_get_currently_bound_driver(d)
        assert cd == None

        driver_override = d.sysfs / "driver_override"
        driver_override.write_text(drivername)

        # do this instead of reloading the vfio_pci kmod to apply driver_override
        bind = Path(f"/sys/module/{modulename}/drivers/pci:{drivername}/bind")
        assert bind.exists() # modprobe vfio_pci
        bind.write_text(d.domain_bus_slot_function)
        cd = pcie_get_currently_bound_driver(d)
        assert cd == drivername

if args.undo_vfio_passthrough is not None:
    print(f"undid vfio passthrough to driver {args.undo_vfio_passthrough}")
    sys.exit(0)

cmdline = [
    "/usr/bin/qemu-system-x86_64",
    "-name",
    f"guest={args.name},debug-threads=on",
    # "-S",
    "-machine",
    # "pc-q35-3.1,accel=kvm,usb=off,vmport=off,dump-guest-core=off",
    #"pc-q35-3.1,accel=kvm,usb=off,vmport=off,dump-guest-core=off,nvdimm=on",
    "pc-q35-3.1,accel=kvm,kernel-irqchip=split,usb=off,vmport=off,dump-guest-core=off,nvdimm=on",

    "-cpu", "host",
    #"-cpu",
    #"host",
    # "EPYC-IBPB,x2apic=on,tsc-deadline=on,hypervisor=on,tsc-adjust=on,arch-capabilities=on,xsaves=on,cmp-legacy=on,perfctr-core=on,virt-ssbd=on,rdctl-no=on,skip-l1dfl-vmentry=on,mds-no=on,monitor=off",

    "-enable-kvm",

    # "host"
    "-m", f"{args.m},slots={len(nvdimms) + 1},maxmem=512G",

    *list(itertools.chain(*[ ["-device", f"vfio-pci,host={d.bus_slot_function}"] for d in vfiodevs ])),

    # "-overcommit",
    # "mem-lock=off",
    "-smp", args.smp,
    # "-uuid",
    # "36c3ce6e-b4f6-49b3-a60a-82dc243ab787",
    # "-no-user-config",
    # "-nodefaults",
    "-rtc",
    "base=utc,driftfix=slew",
    # "-global",
    # "kvm-pit.lost_tick_policy=delay",
    "-no-hpet",
    # "-no-shutdown",
    # "-global",
    # "ICH9-LPC.disable_s3=1",
    # "-global",
    # "ICH9-LPC.disable_s4=1",
    # "-boot",
    # "strict=on",

    #"-device",
    #"pcie-root-port,port=0x10,chassis=1,id=pci.1,bus=pcie.0,multifunction=on,addr=0x2",

    # "-device",
    # "pcie-root-port,port=0x11,chassis=2,id=pci.2,bus=pcie.0,addr=0x2.0x1",
    # "-device",
    # "pcie-root-port,port=0x12,chassis=3,id=pci.3,bus=pcie.0,addr=0x2.0x2",
    # "-device",
    # "pcie-root-port,port=0x13,chassis=4,id=pci.4,bus=pcie.0,addr=0x2.0x3",
    # "-device",
    # "pcie-root-port,port=0x14,chassis=5,id=pci.5,bus=pcie.0,addr=0x2.0x4",
    # "-device",
    # "pcie-root-port,port=0x15,chassis=6,id=pci.6,bus=pcie.0,addr=0x2.0x5",
    # "-device",
    # "pcie-root-port,port=0x16,chassis=7,id=pci.7,bus=pcie.0,addr=0x2.0x6",
    # "-device",
    # "qemu-xhci,p2=15,p3=15,id=usb,bus=pci.2,addr=0x0",
    "-drive",
    f"file={hdd_img},format=qcow2,if=none,id=drive-virtio-disk0",
    "-device",
    "virtio-blk-pci,scsi=off,drive=drive-virtio-disk0,id=virtio-disk0,bootindex=1",

    "-netdev",
    f"tap,br={args.bridge},id=hostnet0,script={upscriptpath},downscript={downscriptpath}",
    "-device",
    #"virtio-net-pci,netdev=hostnet0,id=net0,mac=52:54:00:ba:d4:77,bus=pci.1,addr=0x0",
    "virtio-net-pci,netdev=hostnet0,id=net0,mac=52:54:00:ba:d4:77",

    # "-device",
    # "usb-tablet,id=input0,bus=usb.0,port=1",
    # "-spice",
    # "port=5900,addr=127.0.0.1,disable-ticketing,image-compression=off,seamless-migration=on",
    # "-device",
    # "qxl-vga,id=video0,ram_size=67108864,vram_size=67108864,vram64_size_mb=0,vgamem_mb=16,max_outputs=1,bus=pcie.0,addr=0x1",
    # "-device",
    # "ich9-intel-hda,id=sound0,bus=pcie.0,addr=0x1b",
    # "-device",
    # "hda-duplex,id=sound0-codec0,bus=sound0.0,cad=0",
    # "-device",
    # "virtio-balloon-pci,id=balloon0,bus=pci.5,addr=0x0",
    "-s",
    
    #"-object",
    #"memory-backend-file,id=mem1,pmem,mem-path=/var/lib/libvirt/images/zil-pmem-devl.pmem.8G.img,size=8G,align=2M",
    #"-device", "nvdimm,id=nvdimm1,memdev=mem1",

    #"-object",
    #f"memory-backend-file,id=mem1,pmem=off,mem-path={pmem_img},size={pmem_img.stat().st_size},align=2M",
    #"-device", "virtio-pmem-pci,memdev=mem1,id=nv1,disable-legacy=on",

    *list(itertools.chain(*[ nvd.qemu_args(i+1) for i, nvd in enumerate(nvdimms) ])),
#    "-object", "memory-backend-file,id=mem1,pmem=on,share=on,mem-path=/dev/dax0.0,size=200G,align=2M",
#    "-device", "nvdimm,id=nvdimm1,memdev=mem1,slot=1",

    #"-object", "memory-backend-file,id=mem2,share=on,mem-path=/dev/dax1.0,size=120G,align=2M",
    #"-device", "nvdimm,id=nvdimm2,memdev=mem2,slot=2",
    #"-object", "memory-backend-file,id=mem3,share=on,mem-path=/dev/dax2.0,size=120G,align=2M",
    #"-device", "nvdimm,id=nvdimm3,memdev=mem3,slot=3",

    # "-msg",
    # "timestamp=on",
    "-nographic",
    # "-kernel", "/home/cs/development/external/linux/linux/arch/x86/boot/bzImage",
    # "-append", "root=/dev/vda1 nokaslr ro console=ttyS0,115200",
 #   "-serial", "stdio",
#    "-serial", "tcp::55555,server,nowait",
]

print(cmdline)
os.execvp(cmdline[0], args=cmdline)
