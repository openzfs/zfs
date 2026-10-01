# OpenZFS agent notes

Use these notes for day-to-day changes. Consult `.github/CONTRIBUTING.md`
for PR, commit and detailed style requirements.

## Portability and code

- Common `module/` code targets Linux, FreeBSD and userspace `libzpool`.
  Keep native kernel APIs in `module/os/{linux,freebsd}` and use portability
  interfaces from `include/os/` and `lib/libspl/include/os/`.
- Use Linux compatibility wrappers and `config/kernel-*.m4` feature checks;
  kernel version checks alone miss distribution backports.
- Follow SunOS C style in `CONTRIBUTING.md` and `.editorconfig`: tabs,
  80 columns, 4-space continuations, `return (x)`, one declaration per line.
- `ASSERT*` disappears without debug; `VERIFY*` remains. Never put required
  side effects in `ASSERT`. Validate user input and corrupt disk data with
  errors, not assertions that can panic the host.
- Use `kmem_*`, `vmem_*` and `kmem_cache_*`, not bare `malloc` in common
  kernel code. `KM_SLEEP` requires a context that permits sleeping;
  justify `KM_NOSLEEP` and handle allocation failure.
- Use `sys/zfs_context.h` locks and task queues so common code builds in
  `libzpool`. Preserve lock order, ownership and reference lifetimes on
  error paths too. Comment only non-obvious invariants and reasons, briefly.
- Account for type widths, alignment, byte order and size overflow;
  avoid arithmetic on `void *`.
- Declare common module parameters with `ZFS_MODULE_PARAM`; follow existing
  platform mechanisms elsewhere. Document new parameters in `man/man4/zfs.4`.
- Follow `scripts/spdxcheck.pl` for license tags; preserve third-party licenses
  and keep script shebangs first. Follow the component's update process for
  vendored Lua, zstd and cryptographic code; avoid unrelated changes there.

## Build and test safely

Use a disposable VM for module reloads and ZTS, never a host with live ZFS
pools. `zfs.sh -r` replaces the module stack; ZTS creates and destroys pools,
devices and mounts. `$DISKS` may be overwritten without confirmation, and
`zfs-tests.sh -x` destroys every pool whose name contains `testpool`.
Confirm disposable devices and the test environment if uncertain.

`zloop.sh`, `ztest` and unit tests run in userspace, but `zloop.sh` deletes its
working directory and may change system core-dump settings. Use dedicated
scratch directories and minimal privileges.

```sh
./autogen.sh && ./configure --enable-debug && make -j$(nproc)
make checkstyle
make unit                                 # T=<name> selects one binary
sudo ./scripts/zfs.sh -r                   # disposable VM only
sudo ./scripts/zfs-tests.sh -t <test_name>  # disposable VM only
zfs_source_dir=$(pwd -P)
zfs_scratch_dir=$(mktemp -d /var/tmp/zfs-zloop.XXXXXX) || exit 1
(
    cd "$zfs_scratch_dir" || exit 1
    "$zfs_source_dir/scripts/zloop.sh" -t 600 \
        -f "$zfs_scratch_dir" -c "$zfs_scratch_dir/cores"
)
```

- Use debug builds for correctness testing; also check release builds.
  `make checkstyle` must pass; do not bypass cstyle with exclusions.
  Report actual platforms/tests, missing tools and skipped checks.
- `vcscheck` rejects untracked files: stage intended additions, and remove
  only scratch files you created.
- Verify modules, tools and profiler symbols match the intended build.
  When transferring sources, include working-tree edits: `git archive HEAD`
  omits them.

## Compatibility

- Preserve imports of older pools and tolerate absent new data. If a format
  change prevents older implementations from safely reading or writing,
  add a feature in `include/zfeature_common.h`, register it in
  `module/zcommon/zfeature_common.c`, and document it in
  `man/man7/zpool-features.7`. Gate writes and follow existing activation and
  `spa_feature_incr()`/`decr()` patterns; enabled is not the same as active.
- Send streams are a wire ABI: coordinate incompatible `dmu_replay_record`
  or `DRR_*` changes with maintainers and use appropriate stream flags.
- Preserve ABI for `libzfs`, `libzfs_core`, `libnvpair` and `libzfsbootenv`.
  `make checkabi` compares built libraries with `lib/*/*.abi` on x86_64
  (libabigail >= 2.0). New exports are allowed; do not break existing
  signatures. Run `make storeabi` only for intentional, reviewed ABI updates.
- New ioctls use `zfs_ioctl_register()` and nvlist validation via
  `zfs_keys_*` tables; `libzfs_core` provides stable `lzc_*` wrappers.
- Preserve CLI options, exit status and script-facing output, especially
  machine-readable modes. Property docs live in `man/man7/zfsprops.7` and
  `man/man7/zpoolprops.7`; man pages use mdoc.

## Tests and benchmarks

- ZTS tests: executable `tests/zfs-tests/tests/functional/<area>/<name>.ksh`,
  source `$STF_SUITE/include/libtest.shlib`, use `log_*` helpers,
  `verify_runnable "global"|"both"`, and `log_onexit` cleanup.
  `.cfg`, `.kshlib` and `.shlib` files must not be executable (`testscheck`).
- Register tests in `tests/zfs-tests/tests/Makefile.am` (`make regen-tests`)
  and `tests/runfiles/common.run` or the platform runfile. `make regen-tests`
  runs `make clean` for the whole build; rebuild before testing, or edit the
  entries manually to preserve built artifacts. Missing runfile entries
  exclude tests from that suite, though explicit `-t` still works. Missing
  `Makefile.am` entries prevent installation of the test.
- Use harness devices and `$TESTDIR`/`$TESTPOOL`, never fixed host devices
  or unrelated paths. Clean up pools, datasets, devices and mounts on failure.
- Add behavioural regressions where feasible; check failure before the fix
  and success after. Pure refactors need no artificial reproducer.
  For asynchronous checks, use bounded condition waits and failure diagnostics.
- Observe internals with `zinject`, `zdb`, events, `zfs_dbgmsg` and kstats
  (`/proc/spl/kstat` on Linux, `sysctl kstat.zfs` on FreeBSD), rather than
  adding debug-only ioctls or properties. Do not hide introduced failures in
  `tests/test-runner/bin/zts-report.py.in` expected-failure/skip lists.
- Benchmark performance claims under matching workloads, pool layouts,
  tunables, build options and cache states. Avoid unrelated host/guest load;
  alternate baseline/patch, repeat runs, report settings and variability.
  Use matching release configurations for representative measurements:
  `--enable-debuginfo` adds `-fno-inline` and changes performance.

## Patch preparation

- Base ordinary PRs on `master`; use commit subject/body lines <= 72 characters
  and end the message with `Signed-off-by: Name <email>` (`git commit -s`).
- Keep changes small and independently reviewable; avoid unrelated refactoring.
  `make commitcheck` validates `HEAD`, not the uncommitted patch.
- Override CI's path heuristic with `ZFS-CI-Type: full` or `quick` in the
  commit body when needed; follow `CONTRIBUTING.md` for override precedence.
- Do not commit generated `configure`, `Makefile.in`, `zfs_config.h`, editor
  files or build output; `.abi` updates require the review described above.
