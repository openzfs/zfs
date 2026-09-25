import argparse
import logging
import unittest

from utils import (
    Size,
    ZfsContext,
    add_common_arguments,
    allocate_file,
    allocated_files,
    get_diskdrive_paths,
    get_zfs_mounts,
    path_to_unc,
    paths_to_unc,
    random_key,
    run_cmd,
    setup_logging,
    zpool_create,
)

args: argparse.Namespace
ctx: ZfsContext

logger = setup_logging("tests.tests", logging.DEBUG)
tc = unittest.TestCase()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Process command line arguments."
    )

    add_common_arguments(parser)

    return parser.parse_args()


def preTest(test_name=None):
    logger.info("=" * 60)
    if test_name:
        logger.info(f"Running test: {test_name}")


def main():
    global args
    global ctx

    args = parse_arguments()
    ctx = ZfsContext(args.zfspath)

    logger.debug(f"Path: {args.path}")
    logger.debug(f"Physical devices: {get_diskdrive_paths()}")

    def log_dl(when):
        logger.info(f"Drive letters {when}: {get_zfs_mounts(ctx)}")

    with allocated_files(
        (args.path / f"test{n:02d}.dat", 1 * Size.GIB) for n in range(1, 4)
    ) as bf:
        #######################################################################
        preTest("create zpool backed by single file")
        with zpool_create(ctx, "test01", paths_to_unc(bf[:1])):
            log_dl("after test01 pool create")

        #######################################################################
        preTest("create zpool backed by two files")
        with zpool_create(ctx, "test02", paths_to_unc(bf[:2])):
            log_dl("after test02 pool create")

        #######################################################################
        preTest("create zpool backed by three files")
        with zpool_create(ctx, "test03", paths_to_unc(bf)):
            log_dl("after test03 pool create")

        #######################################################################
        preTest("create mirror zpool backed by two files")
        with zpool_create(ctx, "test04", ["mirror", *paths_to_unc(bf[:2])]):
            log_dl("after test04 pool create")

        #######################################################################
        preTest("create mirror zpool backed by three files")
        with zpool_create(ctx, "test05", ["mirror", *paths_to_unc(bf)]):
            log_dl("after test05 pool create")

        #######################################################################
        preTest("create raidz zpool backed by three files")
        with zpool_create(ctx, "test06", ["raidz", *paths_to_unc(bf)]):
            log_dl("after test06 pool create")

        #######################################################################
        preTest("create raidz1 zpool backed by three files")
        with zpool_create(ctx, "test07", ["raidz1", *paths_to_unc(bf)]):
            log_dl("after test07 pool create")

        #######################################################################
        preTest("snapshot no hang")
        with zpool_create(ctx, "testsn01", paths_to_unc(bf[:1])) as pool:
            log_dl("after testsn01 pool create")

            allocate_file(pool.mount_path / "test01.file", 1 * Size.KIB)
            run_cmd(ctx.ZFS, ["snapshot", "testsn01@friday"])
            allocate_file(pool.mount_path / "test02.file", 1 * Size.KIB)
            run_cmd(ctx.ZPOOL, ["export", "testsn01"])
            pool.destroy = False  # already exported

        #######################################################################
        preTest("snapshot hang")
        with zpool_create(ctx, "testsn02", paths_to_unc(bf[:1])) as pool:
            log_dl("after testsn02 pool create")

            allocate_file(pool.mount_path / "test01.file", 1 * Size.KIB)
            run_cmd(ctx.ZFS, ["snapshot", "testsn02@friday"])
            allocate_file(pool.mount_path / "test02.file", 1 * Size.KIB)
            run_cmd(ctx.ZFS, ["mount", "testsn02@friday"])
            run_cmd(ctx.ZPOOL, ["export", "testsn02"])
            pool.destroy = False  # already exported

        #######################################################################
        preTest("regex for key file")
        with random_key(args.path / "key01.key", 32) as key01:
            key01uri = "file://" + str(path_to_unc(key01)).replace("\\", "/")

            with zpool_create(
                ctx,
                "tank",
                paths_to_unc(bf[:1]),
                zfs_options={
                    "encryption": "aes-256-ccm",
                    "keylocation": key01uri,
                    "keyformat": "raw",
                },
            ):
                log_dl("after tank pool create")

                run_cmd(ctx.ZFS, ["get", "keylocation", "tank"])
                run_cmd(ctx.ZPOOL, ["export", "tank"])

                log_dl("before pool import")

                run_cmd(
                    ctx.ZPOOL,
                    [
                        "import",
                        "-d",
                        bf[0].parent.as_posix(),
                        "-f",
                        "-l",
                        "tank",
                    ],
                )

                log_dl("after pool import")

        #######################################################################
        preTest("run out of drive letters")
        for i in range(1, 26):
            with zpool_create(ctx, f"tank{i}", paths_to_unc(bf[:1])) as pool:
                log_dl(f"after tank{i} pool create")

                allocate_file(pool.mount_path / "test01.file", 1 * Size.KIB)


if __name__ == "__main__":
    main()
