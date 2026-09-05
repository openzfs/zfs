# Linux zvol-swap stall reproducer

This reproducer compares swap on a zvol with an equal-sized raw-swap control.
Both arms run in a disposable QEMU VM with an otherwise-idle ZFS pool imported:

```sh
./run.py raw
./run.py zvol
```

Run the arms as separate commands. Each command uses a fresh VM boot and fresh
scratch devices. On affected builds, the raw arm reaches `pressure-ready`,
while the zvol arm stops producing progress markers with unused swap. Logs,
the last completed phase and the host's classification are retained below
`results/`.

The zvol outcome is timing-dependent. A passing zvol run does not by itself
show that the problem is fixed; retry the zvol arm from a few fresh boots. In
one validation series, both raw controls passed and four of five zvol runs
stalled.

The host needs Python 3.8 or newer, Git, `cloud-localds`,
`qemu-system-x86_64`, `qemu-img` and access to KVM. The first invocation
downloads a pinned, checksum-verified Debian 13 cloud image and boots it with
4 GiB of RAM to install build dependencies and build the current OpenZFS
checkout. Later runs reuse that prepared image from
`$XDG_CACHE_HOME/openzfs-zvol-swap-reproducer`, or `~/.cache` by default.
The checkout must be clean so the cached guest is identified by its exact
commit. Each cached prepared image can consume several GiB; remove old images
from that directory when they are no longer useful.

The preparation VM sees the checkout read-only and writes only to a temporary
status directory. Each test then boots a fresh overlay of the completed image,
with no network, plus two new file-backed scratch disks. Only a read-only test
configuration and that run's result directory are shared with the test VM.
The scratch disks use stable virtio serial numbers and host direct I/O. The
zvol has a fixed 16 KiB block size. The C pressure helper faults anonymous
memory in 8 MiB steps and records remaining RAM and swap after each step.

The guest may become unable to run its own watchdog or cleanup. The host
therefore allows 180 seconds for the guest to create its start marker, then
stops QEMU after 30 seconds without log progress. Separate markers record full
allocation and successful completion; expiry of the no-progress timeout is a
stall. The guest lowers the kernel hung-task timeout to ten seconds, so any
blocked-task reports emitted before QEMU stops are retained in `full.log`.

By default the raw arm expects a pass and the zvol arm expects a stall, so an
unexpected result produces a non-zero exit status. To test a candidate fix,
expect the zvol arm to pass:

```sh
./run.py zvol --expect pass
```

`--expect either` accepts either classified outcome while still rejecting a
boot timeout or unclassified guest failure.

`repro-zvol-swap-stall.sh` is an internal guest payload. Do not invoke it on
the host: it forcibly creates a pool, overwrites a swap device and deliberately
drives the machine into severe memory pressure. The launcher passes no host
block device to QEMU and confines those operations to fresh scratch image
files.
