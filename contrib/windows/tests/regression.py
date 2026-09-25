import argparse
import logging
import os
import pathlib
import unittest

from utils import (
    ZfsContext,
    Size,
    add_common_arguments,
    allocated_files,
    paths_to_unc,
    setup_logging,
    preallocate_file_object,
    get_sizes_from_path,
    get_sizes_from_file,
    get_cluster_size_from_file,
    zpool_create,
)


args: argparse.Namespace
ctx: ZfsContext


logger = setup_logging("tests.regression", logging.INFO)
tc = unittest.TestCase()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Process command line arguments."
    )

    add_common_arguments(parser)

    return parser.parse_args()


def test_preallocation(test_path: pathlib.Path):
    """Tests file sizes when pre-allocating a file.
    Expected values have been confirmed on NTFS, ReFS and FAT32.
    This test currently does not work on a SMB share with the Samba server
    because it reports a fixed cluster size based on a configuration option
    instead of getting the correct value from the underlying file system. ksmbd
    does not have the same issue, it correctly gets the values from the file
    system. So far this is untested with Windows SMB shares.

    See https://github.com/openzfsonwindows/openzfs/issues/281
    See https://bugzilla.samba.org/show_bug.cgi?id=7436

    Args:
        test_path (pathlib.Path): The path where we want to run the test
    """

    fpath = test_path / "testfile.bin"

    try:
        with open(fpath, "wb") as test_file:
            csize = get_cluster_size_from_file(test_file)
            tc.assertGreaterEqual(csize, 512, f"Bad cluster size for {fpath}")

            fsize = get_sizes_from_file(test_file)
            tc.assertEqual(
                fsize,
                {"AllocationSize": 0, "EndOfFile": 0},
                f"Wrong file size after creation of {fpath}",
            )

            preallocate_file_object(test_file, 512)

            fsize = get_sizes_from_file(test_file)
            tc.assertEqual(
                fsize,
                {"AllocationSize": csize, "EndOfFile": 0},
                f"Wrong file size after preallocation of {fpath}",
            )

            test_file.write(b"\x55" * 117)

            fsize = get_sizes_from_file(test_file)
            tc.assertEqual(
                fsize,
                {"AllocationSize": csize, "EndOfFile": 0},
                f"Wrong file size after write to preallocated file {fpath}",
            )

        fsize = get_sizes_from_path(fpath)
        tc.assertEqual(
            fsize,
            {"AllocationSize": csize, "EndOfFile": 117},
            f"Wrong file size after close of preallocated file {fpath}",
        )
    finally:
        if os.path.isfile(fpath):
            os.unlink(fpath)


def test_overwrite_truncation(test_path: pathlib.Path):
    """An overwrite-open must discard the old tail and update EOF.

    Reducing FileAllocationInformation below EOF must also reduce EOF.
    See https://github.com/openzfsonwindows/openzfs/issues/644.
    """
    fpath = test_path / "overwrite-truncation.bin"
    old_data = b"A" * 8192
    new_data = b"B" * 2048

    try:
        fpath.write_bytes(old_data)
        with open(fpath, "wb") as test_file:
            test_file.write(new_data)
            test_file.flush()
            tc.assertEqual(
                get_sizes_from_file(test_file)["EndOfFile"], len(new_data)
            )
        tc.assertEqual(fpath.read_bytes(), new_data)

        fpath.write_bytes(old_data)
        with open(fpath, "r+b") as test_file:
            preallocate_file_object(test_file, 0)
            tc.assertEqual(get_sizes_from_file(test_file)["EndOfFile"], 0)
        tc.assertEqual(fpath.read_bytes(), b"")
    finally:
        if os.path.isfile(fpath):
            os.unlink(fpath)


def run_tests(test_path: pathlib.Path):
    test_preallocation(test_path)
    test_overwrite_truncation(test_path)


def main():
    global args
    global ctx

    args = parse_arguments()
    ctx = ZfsContext(args.zfspath)

    if args.no_pool:
        run_tests(args.path)
    else:
        with (
            allocated_files([(args.path / "test.dat", 1 * Size.GIB)]) as files,
            zpool_create(ctx, "test", paths_to_unc(files)) as pool
        ):
            logger.info(
                f'Created zpool named "{pool.name}", backed by {files}, '
                f"mounted in {pool.mount_path}"
            )

            run_tests(pool.mount_path)

    logger.info("PASSED")


if __name__ == "__main__":
    main()
