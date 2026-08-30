#!/usr/bin/env python3
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
"""Build a disposable guest and run the zvol-swap reproducer in QEMU."""

from __future__ import annotations

import argparse
import datetime as dt
import enum
import fcntl
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import NoReturn

BASE_IMAGE = "debian-13-generic-amd64-20260826-2582.qcow2"
BASE_URL = (
    "https://cloud.debian.org/images/cloud/trixie/20260826-2582/"
    + BASE_IMAGE
)
BASE_SHA512 = (
    "720d9a2d21167e8aa1bb86a8a816658c7beaeec6975c376e15a0761383a869a4"
    "66cbf7fe11c287c989020070309889dd81c37cd412290531245e3562334e05f3"
)

BUILD_MEMORY_MIB = 4096
BUILD_CPUS = 4
TEST_MEMORY_MIB = 768
TEST_CPUS = 2
SWAP_MIB = 768
PRESSURE_EXTRA_MIB = 300
DURATION_SECONDS = 30
BUILD_TIMEOUT_SECONDS = 1800
BOOT_TIMEOUT_SECONDS = 180
STALL_TIMEOUT_SECONDS = 30


class Outcome(enum.Enum):
    PASS = "pass"
    STALL = "stall"
    BOOT_TIMEOUT = "boot-timeout"
    ERROR = "error"


class Expectation(enum.Enum):
    PASS = "pass"
    STALL = "stall"
    EITHER = "either"


class Phase(enum.Enum):
    BOOT = "boot"
    GUEST = "guest"
    POOL = "pool"
    SWAP = "swap"
    ALLOCATION = "allocation"
    LIVENESS = "liveness"
    COMPLETE = "complete"


def die(message: str) -> NoReturn:
    raise SystemExit(f"error: {message}")


def source_commit(repository: Path) -> str:
    status = subprocess.check_output(
        ["git", "-C", repository, "status", "--porcelain"], text=True
    )
    if status:
        die("commit or stash changes before running the reproducer")
    return subprocess.check_output(
        ["git", "-C", repository, "rev-parse", "HEAD"], text=True
    ).strip()


def sha512_file(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def base_image(cache: Path) -> Path:
    image = cache / BASE_IMAGE
    if image.exists():
        if sha512_file(image) != BASE_SHA512:
            die(f"cached image has the wrong checksum: {image}")
        return image

    temporary = cache / f".{BASE_IMAGE}.{os.getpid()}"
    print(f"Downloading {BASE_URL}")
    try:
        with urllib.request.urlopen(  # noqa: SIM117
            BASE_URL, timeout=60
        ) as response:
            with temporary.open("wb") as output:
                shutil.copyfileobj(response, output, 1024 * 1024)
        if sha512_file(temporary) != BASE_SHA512:
            die("downloaded Debian image has the wrong checksum")
        temporary.replace(image)
    except OSError as error:
        die(f"could not download {BASE_IMAGE}: {error}")
    finally:
        temporary.unlink(missing_ok=True)
    return image


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        process.terminate()
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def qemu_command(
    qemu: str, name: str, memory_mib: int, cpus: int
) -> list[str]:
    return [
        qemu,
        "-name",
        name,
        "-machine",
        "q35,accel=kvm",
        "-cpu",
        "host",
        "-m",
        str(memory_mib),
        "-smp",
        str(cpus),
        "-display",
        "none",
        "-serial",
        "stdio",
        "-no-reboot",
    ]


def add_qcow2(command: list[str], path: Path) -> None:
    drive = f"file={path},if=none,id=rootdisk,format=qcow2"
    device = "virtio-blk-pci,drive=rootdisk,bootindex=1"
    command.extend(["-drive", drive, "-device", device])


def add_raw(
    command: list[str], path: Path, identifier: str, serial: str
) -> None:
    drive = f"file={path},if=none,id={identifier},format=raw,cache=none"
    device = f"virtio-blk-pci,drive={identifier},serial={serial}"
    command.extend(["-drive", drive, "-device", device])


def add_share(
    command: list[str], identifier: str, path: Path, *, read_only: bool
) -> None:
    options = f"local,id={identifier},path={path},security_model=none"
    if read_only:
        options += ",readonly=on"
    command.extend(
        [
            "-fsdev",
            options,
            "-device",
            f"virtio-9p-pci,fsdev={identifier},mount_tag={identifier}",
        ]
    )


def overlay(qemu_img: str, backing: Path, output: Path) -> None:
    subprocess.run(
        [
            qemu_img,
            "create",
            "-f",
            "qcow2",
            "-F",
            "qcow2",
            "-b",
            backing,
            output,
        ],
        check=True,
    )


def cloud_seed(
    cloud_localds: str, directory: Path, user_data: Path, key: str
) -> Path:
    meta_data = directory / "meta-data"
    seed = directory / "seed.img"
    meta_data.write_text(
        f"instance-id: zvol-swap-{key}\n"
        "local-hostname: zvol-swap-builder\n"
    )
    subprocess.run(
        [cloud_localds, "--disk-format", "raw", seed, user_data, meta_data],
        check=True,
    )
    return seed


def prepare_guest(
    repository: Path,
    cache: Path,
    base: Path,
    commit: str,
    qemu: str,
    qemu_img: str,
    cloud_localds: str,
) -> Path:
    key = f"{BASE_SHA512[:12]}-{commit[:20]}"
    prepared = cache / f"guest-{key}.qcow2"
    if prepared.exists():
        print(f"Reusing {prepared}")
        return prepared

    temporary = cache / f".{prepared.name}.{os.getpid()}"
    temporary.unlink(missing_ok=True)
    overlay(qemu_img, base, temporary)
    subprocess.run([qemu_img, "resize", temporary, "12G"], check=True)

    try:
        with tempfile.TemporaryDirectory(
            prefix="zvol-swap-prepare."
        ) as run_name:
            run = Path(run_name)
            status = run / "status"
            status.mkdir()
            seed = cloud_seed(
                cloud_localds,
                run,
                repository / "contrib/zvol-swap-stall/prepare-user-data",
                commit[:20],
            )
            log_path = cache / f"prepare-{key}.log"
            command = qemu_command(
                qemu,
                "zvol-swap-prepare",
                BUILD_MEMORY_MIB,
                BUILD_CPUS,
            )
            add_qcow2(command, temporary)
            command.extend(
                ["-drive", f"file={seed},if=virtio,format=raw,readonly=on"]
            )
            add_share(command, "source", repository, read_only=True)
            add_share(command, "status", status, read_only=False)
            command.extend(["-nic", "user,model=virtio-net-pci"])
            print(f"Preparing guest image; log: {log_path}")
            with log_path.open("wb") as log:
                process = subprocess.Popen(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
                try:
                    qemu_status = process.wait(timeout=BUILD_TIMEOUT_SECONDS)
                except subprocess.TimeoutExpired:
                    stop(process)
                    die(
                        "guest preparation exceeded "
                        f"{BUILD_TIMEOUT_SECONDS} seconds"
                    )
                except BaseException:
                    stop(process)
                    raise

            status_file = status / "prepare.status"
            guest_status = (
                status_file.read_text(errors="replace").strip()
                if status_file.exists()
                else "missing"
            )
            if qemu_status != 0 or guest_status != "success":
                sys.stdout.write(log_path.read_text(errors="replace"))
                die(
                    f"guest preparation failed (QEMU status {qemu_status})"
                    f": status is {guest_status!r}"
                )
        temporary.replace(prepared)
    finally:
        temporary.unlink(missing_ok=True)
    return prepared


def watch_progress(
    process: subprocess.Popen[bytes],
    result_dir: Path,
    stall_seconds: int,
    boot_seconds: int,
) -> Outcome:
    pressure_log = result_dir / "pressure.log"
    started_marker = result_dir / "pressure.started"
    done_marker = result_dir / "pressure.done"
    last_size: int | None = None
    last_progress = time.monotonic()
    boot_deadline = last_progress + boot_seconds
    started_seen = False

    while process.poll() is None:
        started = started_marker.exists()
        if started and not started_seen:
            started_seen = True
            last_progress = time.monotonic()
        if done_marker.exists():
            return Outcome.PASS
        if started and pressure_log.exists():
            try:
                size = pressure_log.stat().st_size
            except FileNotFoundError:
                pass
            else:
                if size != last_size:
                    last_size = size
                    last_progress = time.monotonic()

        now = time.monotonic()
        if not started and now >= boot_deadline:
            stop(process)
            return Outcome.BOOT_TIMEOUT
        if started and now - last_progress >= stall_seconds:
            stop(process)
            return Outcome.STALL
        time.sleep(0.2)

    return Outcome.PASS if done_marker.exists() else Outcome.ERROR


def last_phase(result_dir: Path) -> Phase:
    if (result_dir / "pressure.done").exists():
        return Phase.COMPLETE
    if (result_dir / "pressure.ready").exists():
        return Phase.LIVENESS
    if (result_dir / "pressure.started").exists():
        return Phase.ALLOCATION
    if (result_dir / "swap.ready").exists():
        return Phase.SWAP
    if (result_dir / "pool.ready").exists():
        return Phase.POOL
    if (result_dir / "guest.started").exists():
        return Phase.GUEST
    return Phase.BOOT


def run_test(
    backend: str,
    expectation: Expectation,
    prepared: Path,
    result_root: Path,
    commit: str,
    qemu: str,
    qemu_img: str,
) -> int:
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    result_dir = result_root / f"zvol-swap-{backend}-{timestamp}"
    result_dir.mkdir(parents=True, exist_ok=False)
    qemu_version = subprocess.check_output(
        [qemu, "--version"], text=True
    ).splitlines()[0]
    plan = {
        "backend": backend,
        "experimental_unit": "fresh QEMU overlay and scratch devices",
        "base_image_sha512": BASE_SHA512,
        "source_commit": commit,
        "qemu": qemu_version,
        "acceleration": "kvm",
        "memory_mib": TEST_MEMORY_MIB,
        "cpus": TEST_CPUS,
        "swap_mib": SWAP_MIB,
        "pressure_extra_mib": PRESSURE_EXTRA_MIB,
        "stall_seconds": STALL_TIMEOUT_SECONDS,
        "boot_timeout_seconds": BOOT_TIMEOUT_SECONDS,
        "expect": expectation.value,
    }
    (result_dir / "host-plan.meta").write_text(
        "".join(f"{key}={value}\n" for key, value in plan.items())
    )

    with tempfile.TemporaryDirectory(prefix="zvol-swap-qemu.") as run_name:
        run = Path(run_name)
        root = run / "root.qcow2"
        pool = run / "pool.raw"
        raw_swap = run / "swap.raw"
        config = run / "config"
        config.mkdir()
        (config / "config").write_text(
            f"backend={backend}\n"
            "pool_device=/dev/disk/by-id/virtio-zvolswap-pool\n"
            "raw_swap_device=/dev/disk/by-id/virtio-zvolswap-raw\n"
            "pool_name=zvolswap\n"
            f"duration={DURATION_SECONDS}\n"
            f"swap_mib={SWAP_MIB}\n"
            f"pressure_extra_mib={PRESSURE_EXTRA_MIB}\n"
        )
        overlay(qemu_img, prepared, root)
        with pool.open("wb") as stream:
            stream.truncate(2 * 1024**3)
        with raw_swap.open("wb") as stream:
            stream.truncate(SWAP_MIB * 1024**2)

        command = qemu_command(
            qemu,
            f"zvol-swap-{backend}",
            TEST_MEMORY_MIB,
            TEST_CPUS,
        )
        add_qcow2(command, root)
        add_raw(command, pool, "pooldisk", "zvolswap-pool")
        add_raw(command, raw_swap, "swapdisk", "zvolswap-raw")
        add_share(command, "zvolconfig", config, read_only=True)
        add_share(command, "zvolresults", result_dir, read_only=False)
        command.extend(["-nic", "none"])

        full_log_path = result_dir / "full.log"
        with full_log_path.open("wb") as full_log:
            process = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=full_log,
                stderr=subprocess.STDOUT,
            )
            try:
                outcome = watch_progress(
                    process,
                    result_dir,
                    STALL_TIMEOUT_SECONDS,
                    BOOT_TIMEOUT_SECONDS,
                )
                try:
                    qemu_status = process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    stop(process)
                    qemu_status = process.returncode
            except BaseException:
                stop(process)
                raise

        (result_dir / "outcome").write_text(f"{outcome.value}\n")
        phase = last_phase(result_dir)
        (result_dir / "last-phase").write_text(f"{phase.value}\n")
        (result_dir / "qemu.exit").write_text(f"{qemu_status}\n")

    print(
        f"backend={backend} outcome={outcome.value} phase={phase.value} "
        f"results={result_dir}"
    )
    if outcome in (Outcome.BOOT_TIMEOUT, Outcome.ERROR):
        log_tail = full_log_path.read_text(errors="replace").splitlines()[-40:]
        print("--- last 40 lines of full.log ---")
        print("\n".join(log_tail))
    expected_outcome = {
        Expectation.PASS: Outcome.PASS,
        Expectation.STALL: Outcome.STALL,
    }.get(expectation)
    accepted = (
        outcome in (Outcome.PASS, Outcome.STALL)
        if expectation is Expectation.EITHER
        else outcome is expected_outcome
    )
    return 0 if accepted else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("backend", choices=("zvol", "raw"))
    parser.add_argument(
        "--expect", choices=tuple(item.value for item in Expectation)
    )
    args = parser.parse_args()
    expectation = Expectation(
        args.expect or ("pass" if args.backend == "raw" else "stall")
    )

    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        parser.error("KVM is required; /dev/kvm is not readable and writable")

    commands = {}
    for name in ("cloud-localds", "git", "qemu-img", "qemu-system-x86_64"):
        path = shutil.which(name)
        if path is None:
            parser.error(f"missing command: {name}")
        commands[name] = path

    here = Path(__file__).resolve().parent
    repository = Path(
        subprocess.check_output(
            ["git", "-C", here, "rev-parse", "--show-toplevel"], text=True
        ).strip()
    )
    result_root = here / "results"
    result_root.mkdir(exist_ok=True)
    cache = (
        Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
        / "openzfs-zvol-swap-reproducer"
    )
    cache.mkdir(parents=True, exist_ok=True)

    with (cache / "run.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        commit = source_commit(repository)
        base = base_image(cache)
        prepared = prepare_guest(
            repository,
            cache,
            base,
            commit,
            commands["qemu-system-x86_64"],
            commands["qemu-img"],
            commands["cloud-localds"],
        )
        return run_test(
            args.backend,
            expectation,
            prepared,
            result_root,
            commit,
            commands["qemu-system-x86_64"],
            commands["qemu-img"],
        )


if __name__ == "__main__":
    sys.exit(main())
