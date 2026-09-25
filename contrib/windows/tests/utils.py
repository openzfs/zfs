import argparse
import contextlib
import csv
import ctypes
from ctypes import wintypes
import enum
import logging
import msvcrt
import os
import pathlib
import re
import subprocess
import typing


class Size(enum.IntEnum):
    KIB = 1024
    MIB = KIB * 1024
    GIB = MIB * 1024


def setup_logging(name: str, level: int) -> logging.Logger:
    logger = logging.getLogger(name)
    logger.setLevel(level)

    ch = logging.StreamHandler()
    ch.setLevel(level)

    formatter = logging.Formatter(
        "%(asctime)s - %(name)s - %(levelname)s - %(message)s"
    )
    ch.setFormatter(formatter)

    logger.addHandler(ch)

    return logger


def get_next_drive_path() -> pathlib.PureWindowsPath:
    for driveletter in list(map(chr, range(ord("D"), ord("Z") + 1))):
        path = pathlib.PureWindowsPath(f"{driveletter}:\\")
        if os.path.isdir(path):
            continue
        return path
    raise RuntimeError("Unable to find free drive letter")


def decode_console_cp(data: bytes) -> str:
    """Decodes a string of bytes using the active console codepage.

    Args:
        data (bytes): String of bytes in the consoles codepage

    Returns:
        str: The decoded string
    """

    console_cp = ctypes.windll.kernel32.GetConsoleOutputCP()
    return data.decode("cp" + str(console_cp), errors="strict")


def cmd_res_to_str(res: subprocess.CompletedProcess[bytes]) -> str:
    return (
        f"Stdout:\n{decode_console_cp(res.stdout)}\n"
        f"Stderr:\n{decode_console_cp(res.stderr)}"
    )


def run_cmd(
    cmd: pathlib.Path, cmd_args: typing.Iterable[str], **kwargs
) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run([cmd, *cmd_args], **kwargs)


def path_to_unc(path: pathlib.PureWindowsPath) -> pathlib.PureWindowsPath:
    """Adds the extended path prefix \\\\?\\ to a path

    Args:
        path (pathlib.PureWindowsPath): The path to add the prefix to

    Returns:
        pathlib.PureWindowsPath: The path with the prefix added
    """
    return pathlib.PureWindowsPath("\\\\?\\" + str(path))


def paths_to_unc(
    paths: typing.Iterable[pathlib.PureWindowsPath],
) -> typing.Iterable[pathlib.PureWindowsPath]:
    """Adds the extended path prefix \\\\?\\ to multiple paths

    Args:
        paths (typing.Iterable[pathlib.PureWindowsPath]): The paths to add the
            prefix to

    Returns:
        typing.Iterable[pathlib.PureWindowsPath]: The paths with the prefix
            added
    """
    for path in paths:
        yield path_to_unc(path)


def zfs_get_cmd(
    path: pathlib.PureWindowsPath, cmd: str
) -> pathlib.PureWindowsPath:
    """This searches for a ZFS command in the given path. The path can either
    point to a ZFS installation or to a ZFS build output directory.

    Args:
        path (pathlib.PureWindowsPath): The path to search in
        cmd (str): The command to search for

    Returns:
        pathlib.PureWindowsPath: Path to the command
    """

    cmd_path = (path / cmd).with_suffix(".exe")
    if os.path.isfile(cmd_path):
        return cmd_path

    cmd_path = (path / "cmd" / cmd / cmd).with_suffix(".exe")
    if os.path.isfile(cmd_path):
        return cmd_path

    return None


def argparse_as_abspath(path: str) -> pathlib.PureWindowsPath:
    """Converts the given path string to an absolute path object

    Args:
        path (str): The path to convert

    Raises:
        argparse.ArgumentTypeError: When the path is not a directory

    Returns:
        pathlib.PureWindowsPath: The absolute path object
    """

    abspath = os.path.abspath(path)
    if os.path.isdir(abspath):
        return pathlib.PureWindowsPath(abspath)
    else:
        raise argparse.ArgumentTypeError(
            f"{path} is not a valid path, does not exist"
        )


def argparse_as_zfs_abspath(path: str) -> pathlib.PureWindowsPath:
    """Converts the given path string to an absolute path object

    Args:
        path (str): The path to convert

    Raises:
        argparse.ArgumentTypeError: When the path is not a directory or not a
            ZFS installation or build directory

    Returns:
        pathlib.PureWindowsPath: The absolute path object
    """

    abspath = argparse_as_abspath(path)
    if zfs_get_cmd(abspath, "zfs"):
        return abspath
    else:
        raise argparse.ArgumentTypeError(
            f"{path} is not a valid ZFS path, it does not contain zfs.exe"
        )


class ZfsContext:
    def __init__(self, zfspath: pathlib.Path):
        self.zfspath = zfspath

        self.ZFS = zfs_get_cmd(zfspath, "zfs")
        self.ZPOOL = zfs_get_cmd(zfspath, "zpool")
        self.ZDB = zfs_get_cmd(zfspath, "zdb")


class FILE_STANDARD_INFO(ctypes.Structure):
    _fields_ = [
        ("AllocationSize", wintypes.LARGE_INTEGER),
        ("EndOfFile", wintypes.LARGE_INTEGER),
        ("NumberOfLinks", wintypes.DWORD),
        ("DeletePending", wintypes.BOOLEAN),
        ("Directory", wintypes.BOOLEAN),
    ]


class FILE_ALLOCATION_INFO(ctypes.Structure):
    _fields_ = [("AllocationSize", wintypes.LARGE_INTEGER)]


class IO_STATUS_BLOCK(ctypes.Structure):
    _fields_ = [("Status", wintypes.LONG), ("Information", wintypes.PULONG)]


class FILE_FS_SIZE_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("TotalAllocationUnits", wintypes.LARGE_INTEGER),
        ("AvailableAllocationUnits", wintypes.LARGE_INTEGER),
        ("SectorsPerAllocationUnit", wintypes.ULONG),
        ("BytesPerSector", wintypes.ULONG),
    ]


class FILE_INFO_BY_HANDLE_CLASS(enum.IntEnum):
    FileStandardInfo = 1
    FileAllocationInfo = 5


class FS_INFORMATION_CLASS(enum.IntEnum):
    FileFsSizeInformation = 3


def _raise_if_zero(result, func, args):
    if result == 0:
        raise ctypes.WinError(ctypes.get_last_error())


def _raise_if_ntstatus_nonzero(result, func, args):
    if result != 0:
        raise ctypes.WinError(_ntdll.RtlNtStatusToDosError(result))


class DLLHelper:
    def __init__(self, name):
        self.dll_name = name
        self.dll = ctypes.WinDLL(name, use_last_error=True)

    def add_import(
        self,
        name,
        argtypes,
        restype=wintypes.BOOL,
        errcheck=_raise_if_zero,
    ):
        fn = getattr(self.dll, name)
        fn.argtypes = argtypes
        fn.restype = restype
        fn.errcheck = errcheck
        setattr(self, name, fn)


_kernel32 = DLLHelper("kernel32")
_kernel32.add_import(
    "GetFileInformationByHandleEx",
    [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD],
)
_kernel32.add_import(
    "SetFileInformationByHandle",
    [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD],
)


_ntdll = DLLHelper("ntdll")
_ntdll.add_import(
    "NtQueryVolumeInformationFile",
    [
        wintypes.HANDLE,
        IO_STATUS_BLOCK,
        wintypes.LPVOID,
        wintypes.ULONG,
        wintypes.DWORD,
    ],
    restype=wintypes.LONG,
    errcheck=_raise_if_ntstatus_nonzero,
)
_ntdll.add_import(
    "RtlNtStatusToDosError", [wintypes.LONG], restype=wintypes.ULONG
)


def get_cluster_size_from_file(file: typing.IO):
    iosb = IO_STATUS_BLOCK()
    fsi = FILE_FS_SIZE_INFORMATION()

    _ntdll.NtQueryVolumeInformationFile(
        msvcrt.get_osfhandle(file.fileno()),
        iosb,
        ctypes.byref(fsi),
        ctypes.sizeof(fsi),
        FS_INFORMATION_CLASS.FileFsSizeInformation,
    )

    return fsi.SectorsPerAllocationUnit * fsi.BytesPerSector


def get_sizes_from_file(file: typing.IO):
    si = FILE_STANDARD_INFO()

    _kernel32.GetFileInformationByHandleEx(
        msvcrt.get_osfhandle(file.fileno()),
        FILE_INFO_BY_HANDLE_CLASS.FileStandardInfo,
        ctypes.byref(si),
        ctypes.sizeof(si),
    )

    return {
        "AllocationSize": si.AllocationSize,
        "EndOfFile": si.EndOfFile,
    }


def get_sizes_from_path(path: pathlib.Path):
    with open(path, "rb") as f:
        return get_sizes_from_file(f)


def preallocate_file_object(file: typing.BinaryIO, size: int):
    ai = FILE_ALLOCATION_INFO(size)

    _kernel32.SetFileInformationByHandle(
        msvcrt.get_osfhandle(file.fileno()),
        FILE_INFO_BY_HANDLE_CLASS.FileAllocationInfo,
        ctypes.byref(ai),
        ctypes.sizeof(ai),
    )


def allocate_file(path: pathlib.Path, size: int):
    """Creates a file of the given size

    Args:
        name (pathlib.Path): The path to the file
        size (int): The size of the file
    """

    with open(path, "wb") as f:
        f.seek(size)
        f.write(b"0")


@contextlib.contextmanager
def allocated_files(
    files: typing.Iterable[typing.Tuple[os.PathLike, int]]
) -> typing.List[os.PathLike]:
    """Context manager that allocates a file and deletes it when done

    Args:
        path (pathlib.Path): The path to the file
        size (int): The size of the file

    Yields:
        pathlib.Path: Path of the file
    """

    files = list(files)

    for f in files:
        allocate_file(f[0], f[1])

    try:
        yield [f[0] for f in files]
    finally:
        for f in files:
            os.remove(f[0])


@contextlib.contextmanager
def random_key(path: pathlib.Path, size: int) -> pathlib.Path:
    with open(path, "wb") as f:
        f.write(os.urandom(size))

    try:
        yield path
    finally:
        os.remove(path)


def options_to_args(
    flag: str, options: typing.Dict[str, str]
) -> typing.List[str]:
    return [
        f(k, v)
        for k, v in options.items()
        for f in (lambda _, _2: flag, lambda k, v: f"{k}={v}")
    ]


class ZpoolInfo:
    destroy = True

    def __init__(self, name, mount_path):
        self.name = name
        self.mount_path = mount_path

    def __str__(self) -> str:
        return f"zpool {self.name} mounted to {self.mount_path}"


@contextlib.contextmanager
def zpool_create(
    ctx: ZfsContext,
    name: str,
    devices: typing.Iterable[os.PathLike],
    zpool_options: typing.Dict[str, str] = {},
    zfs_options: typing.Dict[str, str] = {},
) -> ZpoolInfo:
    """Context manager that creates a zpool and destroys it when done

    Args:
        devices (typing.Iterable[str]): List of devices or backing files to use
        options (typing.Dict[str, str]): zpool options
        fs_options (typing.Dict[str, str]): zfs options
        size (int): The size of the zpool backing file

    Yields:
        pathlib.Path: Path of the file
    """

    if "driveletter" in zfs_options:
        dl = zfs_options["driveletter"]
        drive_path = pathlib.Path(f"{dl}:\\")
    else:
        drive_path = get_next_drive_path()
        zfs_options["driveletter"] = drive_path.drive.rstrip(":")

    devices = list(devices)
    res = run_cmd(
        ctx.ZPOOL,
        [
            "create",
            "-f",
            *options_to_args("-o", zpool_options),
            *options_to_args("-O", zfs_options),
            name,
            *[str(d) for d in devices],
        ],
    )
    if res.returncode != 0:
        raise RuntimeError("Failed to create zpool")

    pool_info = ZpoolInfo(name, drive_path)

    if not os.path.isdir(drive_path):
        raise RuntimeError("Not mounted after create")

    try:
        yield pool_info
    finally:
        if pool_info.destroy:
            res = run_cmd(ctx.ZPOOL, ["destroy", "-f", name])
            if res.returncode != 0:
                raise RuntimeError("Failed to destroy zpool")

            if os.path.isdir(drive_path):
                raise RuntimeError("Still mounted after destroy")


def repl():
    """This starts a REPL with the callers globals and locals available

    Raises:
        RuntimeError: Is raised when the callers frame is not available
    """
    import code
    import inspect

    frame = inspect.currentframe()
    if not frame:
        raise RuntimeError("No caller frame")

    code.interact(local=dict(frame.f_back.f_globals, **frame.f_back.f_locals))


def add_common_arguments(parser: argparse.ArgumentParser):
    parser.add_argument("--path", type=argparse_as_abspath, required=True)

    # TODO: We need to verify that the zfs path is actually usable because the
    # default path is not passed to `argparse_as_zfs_abspath`.
    program_files = pathlib.PureWindowsPath(os.getenv("ProgramFiles"))
    default_zfs_path = program_files / "OpenZFS On Windows"
    parser.add_argument(
        "--zfspath",
        type=argparse_as_zfs_abspath,
        default=default_zfs_path,
        help="Directory path of either an OpenZFS installation or build"
        " directory",
    )

    parser.add_argument(
        "-np",
        "--no_pool",
        action="store_true",
        default=False,
        help="Don't create a zpool, run tests in path",
    )


ZFS_MOUNT_REGEX = re.compile(r"^([A-Za-z0-9_.\/-]{1,255}) +(.*)$")


def get_zfs_mounts(ctx: ZfsContext) -> typing.Dict[str, os.PathLike]:
    res = run_cmd(ctx.ZFS, ["mount"], capture_output=True)
    ret = {}
    for line in decode_console_cp(res.stdout).splitlines():
        m = ZFS_MOUNT_REGEX.match(line)
        if m:
            ret[m.group(1)] = pathlib.Path(m.group(2))
    return ret


def get_diskdrive_paths():
    res = subprocess.run(
        ["wmic", "diskdrive", "get", "Caption,DeviceId", "/format:CSV"],
        capture_output=True,
    )

    if res.returncode != 0:
        raise RuntimeError(f"Failed to get diskdrive paths: {res.stderr}")

    csv_data = csv.DictReader(
        decode_console_cp(res.stdout).split("\r\r\n")[1:-1]
    )

    ret = {}
    for row in csv_data:
        # We don't make it into a pathlib.Path because this adds a backslash
        # See https://github.com/python/cpython/pull/102003
        ret[row["DeviceID"]] = row["Caption"]
    return ret
