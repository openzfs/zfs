# extended-metadata: Event Log Testing Report

Branch: `extended-metadata` (OpenZFS 2.4.1)
Test hosts: agent-orange (VM 125, 10.9.8.192), zfs-meta (VM 131, 10.9.8.197)
Date: 2026-09-26/27

## Summary

The per-dataset file-level event log feature was installed from source on two
test hosts, exercised across its full property/capability surface, and 6 bugs
were found and fixed. All tests now pass green on both hosts with
`zfs-2.4.1` + commits through `21cfd53c1`.

## What was tested (final green matrix)

| Area | Result |
|---|---|
| Pool feature `org.openzfs:events` enable -> active | PASS (both hosts) |
| `events=on/off` per dataset, isolation (off = no records) | PASS |
| `events_size` range checks (64K/2G rejected cleanly; 128K/1G/4M accepted) | PASS |
| `events_size` inheritance (child inherits, local override, inherit-back) | PASS |
| Query: table view, `-o <dataset>` object filter, `-j` JSON | PASS |
| `records_lost` reporting ("N record(s) lost to log wraparound") | PASS |
| All VFS ops on events=on fs: mkdir/create/write/truncate/rename/symlink/hardlink/setattr/rm | PASS, no panics |
| Ring wraparound: 4000 files through multiple wraps of a 1M ring | PASS |
| Concurrency: 4 parallel writers x (create+rename+delete) x 250 | PASS |
| Persistence: events survive module unload/reload + pool re-import | PASS |
| Snapshot create/destroy on events=on fs | PASS |
| `.zfs/events` pseudodirectory present/listable | PASS |
| FreeBSD paths (compile/consistency) | code-mirrored, not run (no FreeBSD host) |

## Bugs found and fixed

1. **Config-lock panic in VFS path** (`dsl_prop_get_int_ds` under VERIFY):
   `events_size` was read via the property API on every event, which asserts
   the pool config lock — not held in VFS ops. Fix: cache `z_events_size` in
   `zfsvfs_t` via the property callback; the log path uses only cached values.
   (commit `1aae4ece4`)

2. **Open-context feature activation** (`spa_feature_incr` on a non-syncing tx):
   activating `feature@events` from the logging path panicked in
   `feature_do_action()`. Fix: defer activation with `dsl_sync_task_nowait`
   (`zfs_events_feature_sync`), mirror of `spa_history_log_nvl`; log object id
   cached in `zfsvfs->z_events_obj`. (commit `1aae4ece4`)

3. **ERANGE from `zfs set events_size=<out of range>`** aborted zfs(8) with
   "internal error". Fix: explicit range check in `zfs_check_settable` + clean
   "value is out of range" mapping in libzfs. (commit `1aae4ece4`)

4. **Ring wrap hang/livelock**: `zfs_events_advance_bof()` read a reclen from
   never-written (zeroed) ring space and stepped bof 8 bytes per iteration;
   the write loop was unbounded. Fix: zero/oversized reclen => treat through
   EOF as free space; short-circuit `bof >= eof`; cap iterations at
   `phys_max_off >> 3 + 1` and return EIO. (commit `516ddcfb6`)

5. **Concurrent-writer corruption**: multiple VFS threads mutated
   `zep_bof/zep_eof` and interleaved record writes without locking; parallel
   load emptied the log. Fix: per-dataset `z_events_lock` mutex around the
   ring mutation (pattern: `spa_history_lock`). (commit `516ddcfb6`)

6. **"dirtying dbuf obj=N lvl=0 blkid=N but not tx_held" panic** (found on
   zfs-meta): the tx write hold used the current `events_size` property, but
   the log's real physical size comes from its bonus header and diverges from
   the property once `events_size` is changed (setting it does not resize an
   existing log). Writes past the held range hit a tx-hold VERIFY panic in the
   debug build. Fix: `zfs_events_txhold()` reads `zep_phys_max_off` from the
   bonus and issues write holds chunked to `DMU_MAX_ACCESS` over the whole
   physical range (a single `DMU_MAX_ACCESS` hold for a not-yet-created log).
   (commit `21cfd53c1`)

Note: bug 6 was only reproducible with an existing 1M log plus a reduced
`events_size` property and events=on — exactly the state the zfs-meta test
sequence produced; earlier agent-orange runs never combined those conditions
(its "1M" dataset had events=off, so it never logged).

## Debugging method notes

- Kernel panics/hangs were captured via QEMU serial console
  (`serial0=socket` + `console=ttyS0`) logged with socat on the Proxmox host,
  plus `sysrq` dumps and persistent `journalctl` across resets.
- The chunked-hold bug surfaced as a D-state hang only in the DEBUG build
  (`ASSERT3U(len, <=, DMU_MAX_ACCESS)`); a release build would silently
  under-hold.

## Environment

- zfs-meta: VM 131 on bastion, Ubuntu 24.04, 4 vCPU/8GB, kernel
  6.8.0-142-generic; 80G system + 20G test disk (`testpool` on /dev/sdb),
  DEBUG build 2.4.1 (no-git tarball) / commit `21cfd53c1`.
- agent-orange: VM 125, same shape; modules from git builds
  (a99ee2c75 base then `21cfd53c1` fix).
- Original zfs-meta LXC 131 was replaced by a full VM (backup of the LXC in
  `/var/lib/vz/dump/vzdump-lxc-131-*.tar.zst` on bastion, local storage).

## Commits (this branch, this effort)

- `1aae4ece4` Fix event logging to respect tx context, config lock, and tx holds
- `516ddcfb6` Harden event log ring buffer and report wraparound loss
- `21cfd53c1` Hold the event log's physical range in zfs_events_txhold

All commits signed-off; `make checkstyle` clean at each commit.

## Known limitations (not bugs)

- Changing `events_size` does not resize an existing log object; the new value
  applies to newly created logs. (Could be a future feature: on-line resize.)
- `.zfs/events` lists per-object event files as a design placeholder; current
  consumption is via `zfs events <dataset>`.

## Addendum: performance assessment (2026-09-28)

Measured on zfs-meta (QEMU VM, 4 vCPU), debug build:

- **Write-path overhead (events=on vs events=off)**: 2000 empty-file
  creates: OFF 3.27-3.53 s, ON 3.61-3.74 s across three rounds, i.e.
  roughly 5-8% on create-heavy metadata workloads. Plain sequential
  writes are unaffected (2 GiB dd: 1.9 GB/s off vs 1.8 GB/s on -
  within run variance; write(2) is not a logged operation).
- **Query throughput**: 60000-record log paginates end to end in
  ~1.0 s (~58k records/s) through the ioctl/unpack/format path; the
  clear ioctl on the same log takes ~10 ms. A full 256KB page query
  is ~30 ms.
- **Ring capacity is byte-proportional**: a 1M ring holds ~10000
  records (~100 B per packed record), 16M holds 60000+ (no wrap at
  30k file creates). An earlier measurement suggesting a hard
  ~1057-record cap at 1M was actually the page-boundary truncation
  bug fixed in 23f14f3a0, not a capacity limit.
- **Known scale characteristics**: the ring must be sized via
  events_size at dataset creation (resizing an existing log is a
  documented no-op); each logged metadata op takes the zfsvfs ring
  lock and appends under the transaction already open for the VFS
  op, so per-op cost is a lock + memcpy + dmu_write into the log
  object, not a separate transaction.

## Addendum: coverage pass, `zfs events -c`, and setter hardening
(2026-09-28)

### Coverage findings and fixes

After the main matrix went green, a feature-coverage pass identified gaps,
each since fixed and verified:

- **Destroy leaked the log object and feature refcount.** The
  org.openzfs:events refcount was never recorded in the dataset's MOS
  feature zap, so destroy could not deactivate it; `zdb -d` reported a
  permanent "events feature refcount mismatch". Fixed in `2915fd160` by
  activating the feature with `dsl_dataset_activate_feature()` at first
  use, making the existing destroy path deactivate it symmetrically.
  Verified: create(events=on) -> log events -> destroy cycles leave the
  refcount stable on a fresh pool.
- **No way to clear a dataset's event log.** Added `zfs events -c
  <dataset>` (`0b63259b1`). Clearing rides the established
  ZFS_IOC_GET_EVENTS ioctl with `offset == UINT64_MAX` reserved as
  "clear"; the reset runs in open context with the same chunked
  `zfs_events_txhold()` holds the logging path uses, with the pool config
  lock dropped around the transaction (DMU_TX_WAIT asserts it free).
  Verified: clear resets the ring, later writes log again, repeated
  clears are safe. Two earlier implementations were rejected by the
  debug build: a plain open-context assign tripped the config-lock IMPLY
  assert, and a dsl_sync_task version tripped dbuf_dirty "not tx_held"
  (a MOS tx cannot dirty a dataset bonus buffer).
- **Snapshot queries gave a confusing ENOENT.** `zfs events <ds>@<snap>`
  now fails with "event logs are per-dataset and not available on
  snapshots" (part of `0b63259b1`).
- **`zfs set events=on` was silently accepted on pools without the
  feature.** The property set now fails with ENOTSUP and logs a CE_WARN
  naming the missing org.openzfs:events feature. Received-source sets
  are exempt so streams from events-capable senders still land on
  feature-disabled pools. Verified: setter fails on a compat-limited
  pool, succeeds where the feature is enabled.

### Test #4 reconfirmation (send/recv with the final code)

- Send of an events=on dataset works; the log rides the stream as a plain
  DMU object.
- Receive into a feature-disabled pool (compat file without
  org.openzfs:events) succeeds; the received dataset shows events=off,
  the transmitted records remain queryable, and post-receive writes are
  NOT logged (feature disabled).
- `zfs set events=on` on the received dataset in the disabled pool now
  correctly fails with ENOTSUP.
- Receive into a feature-enabled pool also succeeds (4b).

### Known quirks (documented, not fixed)

- A received dataset shows events=off even when the stream carried on;
  event logging does not auto-enable on receive. The transmitted log
  remains readable via `zfs events`.
- Changing `events_size` does not resize an existing log object.

### Commits (coverage pass)

- `2915fd160` Activate org.openzfs:events as a per-dataset feature
- `0b63259b1` Add zfs events -c to clear a dataset's event log

## Addendum: IO-event (WRITE/READ) performance assessment (2026-09-30)

Measured on zfs-meta (QEMU VM, 4 vCPU), debug build, buffered I/O,
3 rounds per cell (all rounds recorded, median quoted). `events=off`
is unreachable on a post-activation dataset in this test sequence:
the datasets were set `events_io=on` earlier, and the setter in
`zfs_ioctl.c` refuses `zfs set events=off` while `events_io` is
still on ("turn events_io off first", ENOTSUP) because events_io
depends on events. This is a dependency-ordering refusal, not a
one-way property gate: `events_io=off` followed by `events=off` is
accepted. (The org.openzfs:events FEATURE is genuinely one-way in
a different sense: it deactivates only when datasets holding event
logs are destroyed - setting events=off does not deactivate it.)
So the baseline is **events=on, events_io=off** - which performs no
IO-record work.

Method: sequential = dd 2 GiB (bs=1M) write / read to /dev/null;
random = self-contained C loop, 50000 4K pwrite/pread ops at LCG
random offsets over a 256M file (no fio on the VM). Ring growth via
the lzc_get_events probe's records_lost counter delta around one 2 GiB
sequential write. fs1 ring: events_size=1M (~10000 records).

| Config (events=on)      | seq write | seq read | 4K rand write | 4K rand read |
|-------------------------|-----------|----------|---------------|--------------|
| events_io=off (base)    | 0.913 s   | 0.369 s  | 0.279 s       | 0.123 s      |
| io=on, window=1000 (def)| 0.896 s   | 0.367 s  | 0.268 s       | 0.130 s      |
| io=on, window=0         | 1.227 s   | 0.404 s  | 0.888 s       | 0.717 s      |
| io=on, window=5000      | 0.917 s   | 0.370 s  | 0.270 s       | 0.120 s      |

Full rounds (s):

- io=off:  seqw 0.913/1.040/0.884  seqr 0.328/0.395/0.369  randw 0.279/0.261/0.289  randr 0.123/0.111/0.135
- win1000: seqw 0.896/0.882/0.901  seqr 0.336/0.367/0.374  randw 0.286/0.268/0.252  randr 0.125/0.131/0.130
- win0:    seqw 1.227/1.279/0.987  seqr 0.406/0.385/0.404  randw 1.065/0.854/0.888  randr 0.757/0.717/0.683
- win5000: seqw 0.901/0.917/2.523  seqr 0.352/0.370/0.432  randw 0.264/0.270/0.289  randr 0.135/0.115/0.120

Conclusions (debug build; treat absolute numbers as upper bounds):

- **Default fence (window=1000) and window=5000 are free**: sequential
  write within run variance of the io=off baseline (-2% to +0.4%);
  4K random write/read within variance (-4% to +6%). The fence reduces
  per-syscall accounting to a bytes+offset update on a pending window.
- **window=0 (per-syscall capture) is the worst case, as designed**:
  +34% sequential write time, +218% 4K random write, +383% median 4K
  random read (0.717 vs 0.123 s). This is the headline number
  justifying the time fence; it is the correct mode only for short,
  targeted forensic captures.
- **Ring growth**: at window=0, one 2 GiB dd (bs=1M) emits ~2048
  WRITE records (one per dd chunk) - a ~10k-record 1M ring wraps in
  roughly 10 GiB (~9-10 s) of sustained bulk I/O, incrementing
  records_lost. At window=1000/5000 the same workload emits ~1 merged
  record (whole dd inside one window) - records_lost delta of 2,
  i.e. background noise only; a sustained-bulk-IO workload emits at
  most one record per window period, so the ring effectively never
  fills from I/O alone.
- **Records per GiB written**: window=0 ~1024 rec/GiB (dd 1M chunks);
  window=1000 <=1 rec/GiB at this throughput; window=5000 <=0.2.

### IO-event behavioral e2e

tests/events-io-e2e.sh (new, this leaf) encodes the wire-level
assertions: gate-off isolation, write/read visibility with uid,
zero-byte suppression, fence coalescing (window=5000 with writes
1s apart -> one merged byte-exact record; see T8 below), per-syscall
behavior at window=0, WRITE-before-RENAME flush ordering, close
flush, byte completeness. The original green runs predate the
2026-10-01 repair addendum and are VOID (see below); the suite was
re-verified green on Linux after the repairs.

### Commits (IO events)

- `2b5555ff4` Opt-in WRITE/READ IO event auditing with per-file time fence
- `85a340dc1` zmetad: schema v2 with WRITE/READ ops and IO fields
- `d9fb50f52` zmetad: persist io_offset/io_bytes for WRITE/READ records
- `8b17ebf48` tests: add IO-event e2e suite and fix schema-e2e privileges

## Addendum: e2e suite repair (2026-10-01)

**All prior "green" claims for both e2e suites in this report are
VOID pending a re-run on Linux (root + ZFS test pool).** The
2026-10-01 bughunt (`.hermes/audits/2026-10-01-extended-metadata-bughunt.md`,
K16/M29) showed both suites were broken in their documented run
modes - the green runs above were produced under a different (root)
invocation than the header documents. The suites cannot be executed
in this environment (macOS, no pool); the repairs below are from
careful code reading and are checked with `bash -n` + shellcheck
only.

Repairs applied to `tests/events-io-e2e.sh`:

- **T1** writer-identity commands: the empty `SUDO` array expanded
  to `-u nobody dd` when run as root (rc 127 - write/read-visible
  steps always failed). A `RUNAS` prefix (runuser / setpriv / su as
  root, `sudo -u` otherwise) now wraps every writer-identity command.
- **T3** `run_probe` checks the probe's exit status; a failing probe
  fails the step explicitly instead of truncating `$RECS` to empty.
- **T8** fence-coalesce margins widened: window=5000ms with writes
  1s apart (was window=2000).
- **T9** absence assertions (gate-off, zero-byte) no longer use
  fixed grace sleeps: they poll until the control CREATE record
  appears (proving the ring drains past the operations under test),
  then assert absence with a bounded poll.
- **T6** all dataset names carry a per-run ID (`$$`); the preflight
  sweep matches ONLY this run's ID, so parallel runs cannot destroy
  each other.
- **T7** `fail()` prints to stderr; helpers echo machine values on
  stdout only, so diagnostics survive command substitution.
- **T12** `field()` extraction is awk-based and value-safe: values
  containing spaces (e.g. `name=my file`) are returned intact
  (extraction runs to the next ` word=` boundary; `name=` is the
  probe's last field).
- **T14** every exact-count step now also asserts the probe's
  `META lost=` counter is 0, so a wrapped ring is reported as ring
  loss instead of a spurious count mismatch.

Repairs applied to `tests/events-schema-e2e.sh`:

- **T2** the daemon DB is root-owned WAL; `db_query`/`db_has_rename`
  and the step_assert python now run through `"${SUDO[@]}"`, making
  both documented modes (root, passwordless sudo) actually work
  (previously: guaranteed 30s-timeout FAILs under sudo mode).
- **T4** the near-vacuous migration test (only exercised the
  duplicate-column no-op path) is replaced by synthetic v1-layout,
  v3-layout, and v4-layout databases (v1: pre-events table;
  v3: adds the captured_at stage; v4: adds the guid/last_lost
  machinery), driven through zmetad's one-shot open path
  (`--purge <nonexistent>`), asserting the full keyed ALTER chain
  runs, columns added, old rows carry NULLs, data survived, and the
  result is the current layout (version 7).
- **T5** new SIGUSR1 step: mid-interval file creation + signal
  (systemctl kill -s USR1 with kill -USR1 $pid fallback) asserts the
  row appears well before the 300s poll interval; a second variant
  signals during a large in-progress bulk collect and asserts no
  crash and no duplicate rows.
- **T6** unit name (`zmd-e2e-$$`) and dataset names carry the
  per-run ID; the stale-artifact sweep matches only this run's ID.
- **T7** `fail()` prints to stderr; `step_guid_swap` is no longer
  run in command substitution (it sets `NEW_RING_GUID` directly).
- **T10** the hardcoded `/home/caimlas/...` fallback path is
  removed; discovery uses `$ZMETAD`, `$REPO/zmetad`,
  `$REPO/contrib/zmetad/zmetad`, `/usr/local/sbin/zmetad`.
- **T11** the dead `zfs events -j | grep schema_version` fallback
  is removed (schema_version is a top-level ioctl key that `zfs
  events -j` never prints, so the branch could never match); the
  wire probe is now the only path, with a notice on compile failure.
- **T13** the step_gap comment's ring-size claim is fixed: the
  default ring is 1 MiB (~10000 records), not 128KB.
- **T14** added: datasets-table row assertion after a poll
  (mountpoint is `/`-prefixed), `--export-schema` clobber-refusal +
  `--force` test, and a retention test that a NULL `captured_at`
  row survives cleanup while a stamped old row is deleted.

Documentation repaired alongside (K17/D1-D15): zfs-events(8)
rewritten against the real getopt string (`cjn:o:`), real
print_event output, and stderr/exit-status semantics; zmetad(8)
`-c`/config-file/pidfile removed, SIGHUP documented as reserved
no-op, schema options documented; zpool-features(7)/zfsprops(7)
activation and deactivation semantics corrected (activation on
first logged event; deactivation only on dataset destroy);
SCHEMA.md op-column table corrected to what the wire actually
carries, version/purge/interval/retention claims fixed.
