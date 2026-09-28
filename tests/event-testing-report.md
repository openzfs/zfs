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
