# zmetad database schema — consumer contract (layout version 8)

This document is the stable contract between the `zmetad` daemon's SQLite
database (`zmetad.db`) and external consumers (e.g. zeta-object). It is
written so a consumer can be implemented without reading `zmetad` source.
Every column name, type, and semantic below is pinned to
`contrib/zmetad/zmetad_db.c` at DB layout version 8.

The database is opened in WAL mode (`PRAGMA journal_mode=WAL`); consumers
may read concurrently with the daemon.


## 1. Stability policy

`meta.db_schema_version = 8` is the stable contract version documented
here.

Evolution rules:

- **Version bumps are additive only**: new columns (added via
  `ALTER TABLE ADD COLUMN`) or new tables. Within a version there are
  **no renames, no type changes, no reinterpretation of an existing
  column or key**.
- **NULL means "field absent from the wire record"** — never a
  fabricated default. Columns whose wire field did not appear in a
  record are NULL; consumers must not invent values for them.
- **Consumers MUST refuse a database whose `db_schema_version` is
  greater than their maximum known version.** This mirrors zmetad's own
  behavior: a newer-layout database is refused with a mismatch error
  ("recreate the database or run an older zmetad"), never opened with
  guessed semantics. Older versions may be handled by applying the
  documented migration steps (Section 2).

Version history:

| Version | Change |
|---------|--------|
| 1 | Initial layout (events core columns, sync_state, meta) |
| 2 | events gains `parent`, `old_parent`, `target`, `old_size`, `attrs` |
| 3 | sync_state gains `ring_guid`; new `datasets` table |
| 4 | events gains `captured_at` (unix seconds at ingest) |
| 5 | events gains `full_path`/`old_full_path`; new `objmap` graph table |
| 6 | sync_state gains `last_lost` (records_lost baseline; NULL = none yet) |
| 7 | sync_state gains `root_id` (persisted dataset root object id; NULL = never learned) |
| 8 | events gains `principal` (opaque application principal tag; NULL = writer did not register one) |

Migration contract: upgrades are performed **in place** by zmetad with
`ALTER TABLE ADD COLUMN`; added columns are NULL for pre-existing rows,
which is the correct representation of "field absent from those
records". A failed-then-retried migration is safe ("duplicate column
name" is treated as success). Consumers reading a v1/v2 database may
apply the same column additions; consumers of a v3 database need no
migration logic.

Two version keys live in `meta` and are independent:

- `db_schema_version` — the SQLite layout version (this document; `8`).
- `events_schema_version` — the wire record schema version (`3`, see
  `contrib/zmetad/events-schema.json`; kept in lockstep with the
  kernel's `ZFS_EVENTS_VERSION`). The record schema evolves additively,
  so zmetad accepts a database whose stored wire version is **older**
  than its loaded schema — absent fields stay NULL, unknown op values
  decode as `UNKNOWN` — and refuses only a stored version that is
  **newer** than the loaded schema (records it may not decode
  faithfully), or a non-numeric stamp (a value it cannot order). A
  missing key is a fresh or pre-versioning database and is accepted.
  Consumers should apply the same rule: an older stored version is
  readable, a newer one is a refuse condition, not a parse attempt.


## 2. Tables

### 2.1 `events`

| Column | Type | NULL | Wire field | Notes |
|--------------|---------|------|------------|-------|
| id | INTEGER | no | — | `PRIMARY KEY AUTOINCREMENT` (insertion order) |
| dataset | TEXT | no | — | dataset name |
| txg | INTEGER | no | `txg` | transaction group of the change |
| timestamp | INTEGER | no | `time` | kernel event time: `gethrtime()` nanoseconds since boot (monotonic, NOT wall clock); ordering/dedup only — see `captured_at` |
| captured_at | INTEGER | yes | — | ingest wall time, unix seconds; NULL in pre-v4 rows. Retention and consumer "when did this appear" queries use this |
| full_path | TEXT | yes | — | dataset-relative path resolved at insert time (e.g. `a/b/c.txt`); NULL when the ancestor chain is unresolvable (row stays PARTIAL, §7). Directory RENAMEs relabel descendants forward — events before the rename keep the old full_path; the rename row's `old_full_path` carries it |
| old_full_path | TEXT | yes | — | RENAME rows: the resolved path of `old_path` at insert time |
| object_id | INTEGER | no | `object` | object ID affected |
| event_type | TEXT | no | `op` | schema enum name (below); unknown op values decode as `UNKNOWN` |
| path | TEXT | yes | `name` | file/dir name, dataset-relative at event time |
| old_path | TEXT | yes | `old_name` | old name (RENAME) |
| uid | INTEGER | yes | `uid` | user ID |
| gid | INTEGER | yes | `gid` | group ID |
| mode | INTEGER | yes | `mode` | file mode at CREATE (permission bits; recorded when the event carries a create/setattr mode) |
| size | INTEGER | yes | `new_size` | size after truncate/setattr |
| io_offset | INTEGER | yes | `io_offset` | IO start offset (WRITE/READ); window's first offset when the events_io fence window is open |
| io_bytes | INTEGER | yes | `io_bytes` | IO byte count (WRITE/READ); summed total of coalesced IOs inside a fence window |
| parent | INTEGER | yes | `parent` | parent object ID |
| old_parent | INTEGER | yes | `old_parent` | old parent (RENAME) |
| target | TEXT | yes | `target` | symlink target (SYMLINK) |
| old_size | INTEGER | yes | `old_size` | size before truncate (TRUNCATE) |
| attrs | INTEGER | yes | `attrs` | changed attr mask (SETATTR) |
| principal | INTEGER | yes | `principal` | opaque application principal tag; NULL when the writer did not register one; supplied by userspace, NOT verified by the kernel - a claim, not evidence |

UNIQUE constraint: `(dataset, txg, object_id, event_type, timestamp)`.

Indexes: `idx_events_dataset_time (dataset, timestamp)`,
`idx_events_object (dataset, object_id)`,
`idx_events_path (dataset, path)`.

Op enum (`event_type` stores the name; wire `op` is uint16, values
outside the enum decode as `UNKNOWN`):

`NONE`, `CREATE`, `REMOVE`, `RENAME`, `LINK`, `SYMLINK`, `TRUNCATE`,
`SETATTR`, `WRITE`, `READ`

Op-constrained optional columns. **What the wire actually carries**
(verified against the kernel emitters in `module/zfs/zfs_events.c` and
the daemon's binds): name-bearing ops (`CREATE`, `REMOVE`, `RENAME`,
`LINK`, `SYMLINK`) send `name`/`parent`; the size/attr/IO ops
(`TRUNCATE`, `SETATTR`, `WRITE`, `READ`) do **not** — their records
carry no `name` and no `parent`, so `path`, `parent`, and (except
where noted) `size` are NULL for those rows:

| Op | Columns populated beyond the always-present four |
|----------|--------------------------------------------------|
| CREATE | `path`, `parent`, `mode`, `uid`, `gid` |
| REMOVE | `path`, `parent` |
| RENAME | `path`, `parent`, `old_path`, `old_parent` |
| LINK | `path`, `parent` |
| SYMLINK | `path`, `parent`, `target` |
| TRUNCATE | `old_size`, `size` (from wire `new_size`) |
| SETATTR | `attrs` |
| WRITE | `io_offset`, `io_bytes`, `uid`, `gid` |
| READ | `io_offset`, `io_bytes`, `uid`, `gid` |

(`uid`, `gid` are optional fields not bound to a specific op: they are
populated whenever the record carries them — in practice CREATE and
WRITE/READ. `principal` is likewise optional on the wire: it is
populated whenever the record carries it, regardless of op, and stays
NULL otherwise — kernel records from writers that did not register a
principal never carry the key, so the column is never fabricated. As
with every field, records may carry fields a given zmetad schema does
not know; unknown wire keys are ignored (forward compatibility), and
new optional fields land here as additional nullable columns. Consumers
MUST NOT expect `path`, `parent`, or `size` on
TRUNCATE/SETATTR/WRITE/READ rows: identify those events by
`object_id`, resolving the object to a name from the dataset's
CREATE/RENAME history. WRITE/READ exist only when `events_io` is
enabled and are coalesced by the `events_io_window` fence —
`io_offset` is the window's first offset, `io_bytes` the summed
total.)

The four always-present columns are `txg`, `timestamp` (`time`),
`object_id` (`object`), and `event_type` (`op`).

**LINK is last-link-wins** for path resolution: an object's location
map (`objmap`, and consequently `full_path` resolution) holds a single
`(name, parent)` per object, so a hard LINK overwrites the object's
previous mapping with the new link's name/parent. Older link paths
remain visible only in the historical event rows.

### 2.2 `sync_state`

One row per polled dataset.

| Column | Type | NULL | Notes |
|------------|---------|------|-------|
| dataset | TEXT | no | `PRIMARY KEY` |
| last_offset | INTEGER | no | watermark: next read offset into the kernel event log |
| last_sync | INTEGER | no | unix time of the last successful poll |
| ring_guid | INTEGER | yes | identity of the kernel event log instance; see Section 6 |
| last_lost | INTEGER | yes | previous poll's cumulative `records_lost`; NULL means no baseline yet |
| root_id | INTEGER | yes | dataset root object id as reported by the kernel (`root_objid`); NULL/0 = never learned — see below |

`last_offset` and `ring_guid` are always written together in a single
statement, so a persisted offset and its ring identity are never
observed torn. That statement **preserves a previously stored
`ring_guid` when the incoming one is absent/NULL** (legacy-kernel
reply): a stored identity is never erased by a reply that lacks the
key. `last_lost` is written on every poll that observed the counter;
NULL on rows that predate layout 6 or have not yet seen a poll.
`root_id` is written whenever the kernel reports `root_objid` and is
never touched by the offset writer (each column has exactly one
writer).

`root_id` semantics: **0/NULL means the root object id was never
learned — a legacy kernel that does not send `root_objid` — and the
daemon falls back to the graph-emptiness heuristic for path
resolution.** Once a root id has been learned from a reply that
carried it, later replies lacking the key (e.g. an older kernel
running against the same database) resolve against the STORED id
instead of degrading to PARTIAL permanently.

`meta` also carries per-dataset `purge_epoch:<dataset>` keys: a
monotonic counter bumped atomically by `--purge <dataset>`. The daemon
compares **only its own dataset's** epoch each poll and re-arms its
in-memory watermark and loss baseline for that dataset, so purging a
live dataset does not manufacture a spurious regression gap row — and
purging dataset A does not re-arm datasets B and C. The pre-layout-7
global `purge_epoch` key may still be present in migrated databases;
it is read once as the initial baseline and no longer updated.

### 2.3 `datasets`

| Column | Type | NULL | Notes |
|------------|------|------|-------|
| dataset | TEXT | no | `PRIMARY KEY` |
| mountpoint | TEXT | no | mountpoint recorded at collect time |
| last_seen | INTEGER | yes | unix time of the most recent collect that saw the dataset |

Refreshed (`INSERT OR REPLACE`) on every dataset sighting, so
mountpoint changes self-heal. The value is stored **as-is**: non-/
mountpoints such as `none` or `legacy` are stored verbatim — consumers
must filter/prefix-match accordingly. Rows whose `last_seen` predates
the current poll cycle are pruned (`zmetad_db_prune_stale_datasets`):
a dataset destroyed or with events disabled stops being attributed
paths. This table is not deleted by `--purge` (the mapping row stays);
it holds no records, only the mapping.

### 2.4 `objmap`

The objid → (name, parent) graph the daemon maintains to resolve
`full_path` at insert time: one row per known object per dataset.
A hard LINK overwrites the object's row (last-link-wins, §2.1);
REMOVE deletes it. Consumers may read it, but it is an implementation
detail of the resolver — the durable per-row truth remains
`path`/`parent` plus `full_path`. `--purge` deletes a dataset's objmap
rows; retention never touches them.

### 2.5 `gaps`

Permanent completeness record. One row per loss event observed at poll
time.

| Column | Type | NULL | Notes |
|-------------|---------|------|-------|
| id | INTEGER | no | `PRIMARY KEY AUTOINCREMENT` |
| dataset | TEXT | no | dataset name |
| detected | INTEGER | no | unix time the loss was detected |
| from_offset | INTEGER | yes | start of the affected offset range; NULL when no range applies |
| to_offset | INTEGER | yes | end of the affected offset range; NULL when no range applies |
| lost | INTEGER | no | sentinel, see below |

`lost` semantics:

| Value | Meaning |
|-------|---------|
| > 0 | exactly that many records were lost since the previous poll (ring-wrap overwrite or collector queue overflow — the cumulative `records_lost` counter delta) |
| 0 | watermark regression detected (records between the regression boundary and the new offset were never captured); count unknown |
| -1 | ring replaced (identity swap); count unknown — history before this boundary belonged to a different kernel event log. Only the ring-replace path writes this sentinel. |

Row lifecycle:

- Rows are **never rewritten**.
- Rows are **never deleted by retention** (`zmetad_db_cleanup` touches
  `events` only). They are therefore lifetime counts per dataset.
- Rows are deleted **only** by `zmetad --purge <dataset>`, which removes
  the dataset's `events`, `gaps`, `sync_state`, and `objmap` rows and
  clears the kernel ring.

### 2.6 `meta`

| Column | Type | NULL | Notes |
|-------|------|------|-------|
| key | TEXT | no | `PRIMARY KEY` |
| value | TEXT | no | string value |

Known keys: `db_schema_version` (`"8"`), `events_schema_version`
(`"3"`), and `purge_epoch:<dataset>` (one monotonic integer per
purged dataset; bumped atomically by `--purge`). The legacy global
`purge_epoch` key survives in databases migrated from layout ≤ 6 but
is no longer written.


## 3. Deduplication / insertion

Event inserts use `INSERT OR IGNORE` against the
`UNIQUE(dataset, txg, object_id, event_type, timestamp)` key. Re-polling
or re-collecting an overlapping range is therefore **idempotent**:
duplicate rows never appear, and a duplicate insert is not an error.
Consumers that replay inserts must rely on the same key, not on `id`.


## 4. Loss accounting formulas (#9)

Per dataset, over the `gaps` table:

- **knownLost** (cumulative known records lost, lifetime):
  `SELECT SUM(lost) FROM gaps WHERE dataset = ? AND lost > 0`
- **ringSwaps** (lifetime ring replacements):
  `SELECT COUNT(*) FROM gaps WHERE dataset = ? AND lost = -1`
- Watermark regressions (count unknown):
  `SELECT COUNT(*) FROM gaps WHERE dataset = ? AND lost = 0`

When a wire-compatible response requires a single `recordsLost` figure,
it is **knownLost**. Ring swaps MUST be reported as a separate
boolean/count — never folded into a record count: a `-1` row carries no
count, and treating it as 1 or summing it corrupts the figure. (This
matches zmetad's own stats, which count the three gap classes
separately.)

### 4.1 GET_EVENTS reply keys

The kernel's `ZFS_IOC_GET_EVENTS` reply (consumed page by page) carries
the counters the formulas above derive from. A consumer reading the wire
directly must treat them as follows:

- `next_offset` — the **resume cursor**: the ring's end-of-window offset
  for the next read. Pagination resumes here, so records already
  delivered in the current window are not re-delivered when a later call
  clamps its start offset below the cursor. A zero cursor means the log
  is exhausted; a cursor that fails to advance means the end of the
  readable window.
- `records_lost` — the ring's **cumulative** count of overwritten
  records since the log was created. It is monotonic for a live ring
  (only a ring replacement resets it, §6). The per-poll loss is its
  delta against the previously persisted baseline, which is exactly the
  `lost > 0` gap rows of §2.5.
- `records_undecodable` — records the cursor consumed in **this single
  reply** but could not decode (per-reply, NOT cumulative; the key is
  omitted when there are none, and is absent on older kernels). zmetad
  records it as its **own** `gaps` row spanning the page once the
  watermark is durably advanced, but deliberately keeps it OUT of the
  cumulative `records_lost` delta that drives loss detection: folding it
  in would re-count every undecodable record on each later poll.
  Consumers must therefore never add it to `records_lost` (which stays
  monotonic); it is accounted for once, as its own gap row.


## 5. Path resolution (#6)

Resolve an event path to its dataset using the `datasets` table only —
no kernel calls:

1. Load all `(mountpoint, dataset)` rows.
2. Filter out rows whose `mountpoint` is not a `/`-prefixed path
   (verbatim `none`/`legacy` entries).
3. Match the path against the longest `mountpoint` that is an exact
   match or an ancestor directory of the path.

The path-relative remainder of the path is relative to the dataset.


## 6. Ring identity rules

`sync_state.ring_guid` identifies the kernel event log instance a
watermark belongs to.

- **NULL (or 0 when read through the daemon's accessor) = identity
  unknown**: no `sync_state` row yet, a pre-v3 row, or a legacy kernel
  reply that never carried `ring_guid`. It does not mean "no ring".
- A stored non-NULL GUID is **never erased** by a reply that lacks the
  key: the watermark upsert keeps the stored identity when the
  incoming one is NULL, so mixed-version operation (kernel downgrade,
  module upgrade while the daemon runs) does not lose swap detection.
- The GUID changes iff the kernel ring was replaced (dataset
  destroy/recreate, receive). On a detected change zmetad:
  1. writes one `gaps` row with `lost = -1` (the swap boundary;
     `from_offset` NULL, `to_offset` = the last offset of the old log),
  2. resets the watermark to 0 and persists it together with the **new**
     GUID,
  3. clears the stored `last_lost` baseline (it belonged to the old
     ring's counter; a new-ring counter above the old baseline must not
     produce a cross-ring delta).
- Consumers should therefore **segment per-epoch queries by the
  `lost = -1` gap rows**: records before a `-1` row's position belong to
  a previous log instance and must not be joined with records after it
  (offsets restart at 0 for each new ring).


## 7. Reconstruction contract (#10)

To reconstruct path state per dataset:

- Build an `object_id -> (name, parent)` graph from **all** rows of the
  dataset (`event_type` CREATE/RENAME/LINK/SYMLINK/REMOVE etc.), applied
  in `(txg, id)` order — `txg` orders transaction groups, `id` breaks
  ties within a txg in capture order. A LINK applies last-link-wins:
  the object's mapping becomes the newest link's `(name, parent)`.
- A row is **PARTIAL** when its ancestor chain is incomplete — i.e. an
  ancestor's CREATE fell inside a `gaps` loss range (Section 4), so no
  full path can be proven. On layout ≥ 5 this is exactly the name-bearing
  row (CREATE/RENAME/LINK/SYMLINK and IO records) whose `full_path` is
  NULL; WRITE/READ/TRUNCATE/SETATTR never carry a `full_path` — their
  NULL is not PARTIAL. Consumers on older layouts reconstruct and test
  resolvability themselves.
- Serve PARTIAL rows under **conservative match** only:
  - exact match on the bare name, or
  - a queried key ending in `"/" + bare name`.
- **Never hide the only surviving record of an object**: a PARTIAL row
  is still evidence the object existed; excluding it entirely loses
  information.
- **Never fabricate a full path for a partial row.** If the ancestor
  chain cannot be proven, do not synthesize one.
- On layout ≥ 5, a non-NULL `full_path` is authoritative as of the
  row's own event: serve it directly, no reconstruction. Rows carry
  the path as of that event; a later directory RENAME does not rewrite
  history (the rename row's `old_full_path` links the before-path).


## 8. Freshness (#5)

The database lags live filesystem state by at most one poll interval
(default 30 seconds; `-i`/`--interval`, minimum 1). `SIGUSR1` forces an
immediate out-of-band collect, after which the database is current as of
that collect. See zmetad(8) for CLI details.


## 9. Operational facts

- **Retention** (`-r`/`--retention <days>`, default 90; must be
  1..36500 — zero, negative, and non-numeric values are rejected):
  deletes `events` rows whose `captured_at` is older than the cutoff,
  then runs `VACUUM`. Rows with a **NULL `captured_at`** (pre-v4
  rows) are **never deleted** by retention, regardless of age.
  **Scope is events only** — `gaps`, `sync_state`, and `datasets` are
  untouched, which is what makes the gap counts in Section 4
  lifetime figures.
- **`zmetad --purge <dataset>`** (one-shot mode: no daemonize, no poll
  loop): deletes the dataset's rows from `events`, `gaps`,
  `sync_state`, **and `objmap`**, then clears the dataset's in-kernel
  event ring and bumps the dataset's `purge_epoch:<dataset>` meta key
  (atomically). Its report line prints the **events and gaps counts
  only** (`purged <ds>: N events, M gaps removed; kernel ring
  cleared`). This is the **only** mechanism that removes `gaps` rows.
  The `datasets` mapping row is left in place.
- **Poll loop**: collects every `poll_interval` seconds (1-second sleep
  granularity); each poll persists the watermark + ring GUID, appends
  new events (dedup as in Section 3), refreshes the `datasets`
  mountpoint row, and inserts gap rows per Section 2.4 when loss is
  detected.


## 10. Spool envelope (NDJSON stream)

Besides the database, zmetad can mirror everything it ingests to an
NDJSON spool file for log-pipeline tailers (syslog-ng, vector, ...).
It is **off by default**; setting a non-empty `spool_path` in
`/etc/zmetad.conf` enables it (see zmetad(8), SPOOL FILE). The spool
is a *derived stream of the database*: every line below corresponds to
an events or gaps row (or the watermark reset of a ring swap) that was
committed first. A consumer may treat the two as parallel views of the
same facts.

Envelope format: one compact JSON object per line, UTF-8, `LF`
delimited. Every line carries the envelope header `zmetad` (constant
`1`), `type`, `dataset`, and `ts` (unix seconds). Three types exist,
verbatim:

```json
{"zmetad":1,"type":"event","dataset":"tank/data","ts":1696312345,"rec":{"txg":15042,"object":128,"op":"TRUNCATE","new_size":0}}
{"zmetad":1,"type":"epoch","dataset":"tank/data","ts":1696312346,"old_guid":111,"new_guid":222}
{"zmetad":1,"type":"gap","dataset":"tank/data","ts":1696312347,"lost":37}
{"zmetad":1,"type":"gap","dataset":"tank/data","ts":1696312347,"swap":true}
```

- `event` — one record as inserted into `events` (Section 2.1). `rec`
  is the wire record serialized schema-driven, **in schema field
  order**, with the same field semantics as the table columns (wire
  names, e.g. `object`, `op`); `op` carries the **enum name**
  (`TRUNCATE`), exactly what `event_type` stores; absent wire fields
  are omitted (the JSON form of the NULL column rule). `ts` is the
  record's `captured_at` (ingest time; one value per collect batch).
- `epoch` — ring identity change (Section 6): `old_guid` → `new_guid`.
  The stream form of the `lost = -1` gap row's boundary.
- `gap` — loss boundary (Section 2.5), two forms: `"lost":N` with
  N ≥ 0 (exact lost count, or `0` = count-unknown regression) and
  `"swap":true` (ring replaced; the `-1` sentinel rendered as a JSON
  boolean — never as a numeric count). The sentinel rules of
  Section 2.5/4 apply unchanged.

Ordering guarantee: **markers precede the post-boundary records.** A
`gap`/`epoch` marker for a boundary is always written to the stream
before any `event` line of the page(s) after that boundary, so a
consumer can segment the stream exactly like the gaps-table consumer
segments its queries (Section 6). Line writes are `O_APPEND` and
line-atomic: a tailer never observes a partial line, and no line ever
spans a rotation.

Best-effort semantics: a spool failure (ENOSPC, I/O error) never
blocks or fails the daemon and never touches the database — the DB
commit is the integrity record. After a write error the spool
**poisons** itself for the rest of the collect cycle (one warning per
cycle, on stderr/syslog), drops the remaining lines of that cycle, and
re-arms at the next cycle start. Consequences for consumers: the spool
is **lossy by design** — a gap in the *stream* does not mean a gap in
the *database*; authoritative completeness lives in `events`/`gaps`
(Sections 2.1/2.5). Consumers needing completeness must not rely on
the spool alone.

Rotation: size-based, single generation. Before a write that would
push the file past `spool_max_bytes` (default 64 MB, range 1 MB–1 GB),
the file is closed, renamed to `<spool_path>.1` (overwriting any
previous `.1`), and a fresh file is opened. Because the check runs
before the whole line is appended, **no line is ever split across a
rotation**. Tailers should follow both `<spool_path>` and
`<spool_path>.1`.

Consumer recipe (tailer):

1. Follow the spool file (and `.1`); parse each line as JSON. Reject
   (counter) any line whose `zmetad` != 1 or whose `type` is unknown —
   that is a forward-compatibility signal, not a parse error.
2. `type == "event"`: the line is the same fact as an `events` row;
   map `rec` fields per the Section 2.1 table (`dataset` + `rec`
   together identify the record; `ts` is `captured_at`).
3. `type == "gap"` with `"swap":true`, or `type == "epoch"`: segment
   the stream here, exactly as a DB consumer segments at `lost = -1`
   rows (Section 6): records before the marker belong to the previous
   kernel event log.
4. `type == "gap"` with `"lost":N`: loss boundary; N follows the
   Section 2.5 sentinel table (N = 0 is "count unknown", never "no
   loss").
5. Treat missing lines as possible (best-effort, above); do not use
   the spool for completeness judgments.
