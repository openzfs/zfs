#!/usr/bin/env bash
set -euo pipefail
set -x
# Following instructions in
# https://www.prakashsurya.com/post/2017-09-28-generating-code-coverage-reports-for-zfs-on-linux/

GCOV_KERNEL="/sys/kernel/debug/gcov"

cd "$(dirname "${BASH_SOURCE[0]}")"


action="$1"

logdir="$2" # specify /dev/null for interactive-only use

if [ ! -d "$logdir" ]; then
    echo "$logdir must be a directory"
    exit 1
else
    logdir="$(readlink -f "$logdir")"
fi

touch "$logdir/.test" || (echo "cannot create files in logdir=$logdir"; exit 1)
rm "$logdir/.test"

ZFS_BUILD="$(readlink -f .)"

setup_log() {

    local file="$1"
    test ! -f "$file" || (echo "file $file must not exist"; exit 1)

    # https://stackoverflow.com/questions/3173131/redirect-copy-of-stdout-to-log-file-from-within-bash-script-itself
    # Redirect stdout ( > ) into a named pipe ( >() ) running "tee"
    exec > >(tee -i "$file")
    # Without this, only stdout would be captured - i.e. your
    # log file would not contain any error messages.
    # SEE (and upvote) the answer by Adam Spiers, which keeps STDERR
    # as a separate stream - I did not want to steal from him by simply
    # adding his answer to mine.
    exec 2>&1
}

# ok, now to business

common_pre()
{
    echo "Reset in-kernel gcov state"
    sudo bash -c 'echo 1 > /sys/kernel/debug/gcov/reset'

    echo "Remove existing data files (regardless whether copied from kernel or written by userspace)"
    find . -name '*.gcda' -delete

    rm -rf "$logdir/$action"
    mkdir "$logdir/$action"

    git describe --always > "$logdir/$action/git_describe_always"

    setup_log "$logdir/$action/log.txt"
}

common_post()
{
        rm -rf zfs-2.0.0-coverage
        rm -f zfs-2.0.0-coverage.info
        CODE_COVERAGE_BRANCH_COVERAGE=1 make code-coverage-capture
        cp -vna zfs-2.0.0-coverage zfs-2.0.0-coverage.info "$logdir/$action/"

        find . -name "*.gcno" -exec sh -c 'tar --append --file '"$logdir/$action/"'raw.tar $0' '{}' \;
        find . -name "*.gcda" -exec sh -c 'tar --append --file '"$logdir/$action/"'raw.tar $0' '{}' \;
}

case "$action" in
    userspace)

        test ! -f core || (echo "remove coredump $(readlink -f core) first, ztest will fail otherwise"; exit 1)

        common_pre

        pushd tests/zfs-tests/cmd/zilpmem_test
        ZFS_ZILPMEM_TEST_OPS="avx512" cargo test --release
        ZFS_ZILPMEM_TEST_OPS="libpmem" cargo test --release
        popd

        set +e
        for t in 3600; do
            time sudo scripts/zloop.sh -t $t -- -o vdev_file_dax_mmap_sync=0 -z pmem
            time sudo scripts/zloop.sh -t $t -- -o vdev_file_dax_mmap_sync=0 -z lwb
        done
        set -e

        common_post

        ;;
    kernel)

        common_pre

        # ok, let's do this

        sudo scripts/zfs.sh -u && sudo scripts/zfs.sh
        sudo bash -c 'echo 1 > /sys/module/spl/parameters/spl_panic_halt'

        sudo bash -c 'echo 0 > /sys/module/zfs/parameters/zfs_zil_itxg_bypass'

		sudo bash -c 'echo 1 > /sys/module/zfs/parameters/zil_default_kind'
        set +e
		scripts/zfs-tests.sh -T slog -x
        lwb="$?"
        set -e
		sudo bash -c 'echo 2 > /sys/module/zfs/parameters/zil_default_kind'
        set +e
		scripts/zfs-tests.sh -T slog -x
        pmem="$?"
        set -e
        echo "lwb=$lwb pmem=$pmem"

        # copy coverage data from debugfs
        echo "Allow access to gcov files as a non-root user"
        sudo chmod -R a+rx "$GCOV_KERNEL"
        sudo chmod a+rx /sys/kernel/debug
        echo "Copy out the data"
        pushd "$GCOV_KERNEL$ZFS_BUILD" >/dev/null
        find . -name "*.gcda" -exec sh -c 'cp -v $0 '$ZFS_BUILD'/$0' {} \;
        find . -name "*.gcno" -exec sh -c 'cp -vdn $0 '$ZFS_BUILD'/$0' {} \;
        popd >/dev/null
        echo "Done copying out data"

        # only unload the module _after_ we have sucked out the coverage data
        sudo scripts/zfs.sh -u

        common_post

        ;;
    *)
        echo "invalid action $action"
        exit 1
esac


