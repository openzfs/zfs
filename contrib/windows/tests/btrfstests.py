import os
import argparse

import subprocess

# from pathlib import Path, PurePosixPath, PureWindowsPath, WindowsPath
from pathlib import PureWindowsPath

# from pprint import pprint

import time


# import json

import logging

logging.basicConfig(level=logging.DEBUG)


print("Printed immediately.")


def parse_arguments():
    parser = argparse.ArgumentParser(description='Process command line '
                                     'arguments.')
    parser.add_argument('-path', type=dir_path, required=True)
    return parser.parse_args()


def dir_path(path):
    if os.path.isdir(path):
        return path
    else:
        raise argparse.ArgumentTypeError(f"readable_dir:{path} is not a valid"
                                         "path")


def get_DeviceId():
    magic_number_process = subprocess.run(
        ["wmic", "diskdrive", "get", "DeviceId"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

#   https://github.com/sir-ragna/dddddd
#   get DeviceId

    a = magic_number_process.stdout.decode(encoding='UTF-8', errors='strict')
    b = a.replace("\r\r\n", "\r\n")

    c = b.splitlines()

    d = [x.split() for x in c]

    e = [x[0] for x in d if len(x) > 0 and x[0] not in "DeviceID"]

    e.sort()

#   print(e)

#   print([x.encode(encoding='UTF-8') for x in e])

#   import csv

#   with open('csv_file.csv', 'w', encoding='UTF8') as f:
#       writer = csv.writer(f, dialect='excel', quoting=csv.QUOTE_ALL)
#
#       for row in e:
#           writer.writerow([row])

    return e


def allocate_file(name, size):
    with open(name, 'wb') as f:
        f.seek(size)
        f.write(b'0')


def delete_file(name):
    if os.path.exists(name):
        os.remove(name)
    else:
        print("The file does not exist")


def get_driveletters():
    magic_number_process = subprocess.run(
        ["C:\\Program Files\\OpenZFS On Windows\\zfs.exe", "mount"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

#   b'test01                          H:\\ \r\ntest02             I:\\ \r\n'

    a = magic_number_process.stdout.decode(encoding='UTF-8', errors='strict')

    c = a.splitlines()

    logging.debug("get_driveletters() {}".format(c))

#    print("get_driveletters() debug",c)

    d = [x.split() for x in c]

    logging.debug("get_driveletters() {}".format(d))

    return d

#      run: '& "C:\Program Files\OpenZFS On Windows\zfs.exe" mount'


def create_pool(name, file):
    magic_number_process = subprocess.run(
        ["C:\\Program Files\\OpenZFS On Windows\\zpool.exe",
            "create", "-f", name, file],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    return magic_number_process


def destroy_pool(name):
    magic_number_process = subprocess.run(
        ["C:\\Program Files\\OpenZFS On Windows\\zpool.exe",
            "destroy", "-f", name],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    return magic_number_process


def zpool(*args):
    magic_number_process = subprocess.run(
        ["C:\\Program Files\\OpenZFS On Windows\\zpool.exe", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    return magic_number_process


def zfs(*args):
    magic_number_process = subprocess.run(
        ["C:\\Program Files\\OpenZFS On Windows\\zfs.exe", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    return magic_number_process


def run(args):
    d = {"zfs": "C:\\Program Files\\OpenZFS On Windows\\zfs.exe",
         "zpool": "C:\\Program Files\\OpenZFS On Windows\\zpool.exe"}
    arglist = list(args)
    try:
        cmd = d[arglist[0]]
    except Exception:
        cmd = arglist[0]
    result = subprocess.run(
        [cmd, *arglist[1:]],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    return result


def tounc(name):
    q = "\\\\?\\" + str(name)
    return q


def runWithPrint(cmd):
    print(" ".join(cmd))
    ret = run(cmd)
    logging.debug(str("args={}".format(" ".join(ret.args))))
    logging.debug(str("returncode={}".format(ret.returncode)))
    logging.debug(str("stdout={}".format(ret.stdout)))
    logging.debug(str("stderr={}".format(ret.stderr)))

    return ret


def preTest(testName=None):
    print("=" * 20)
    if testName is not None:
        print("Name:", testName)

    get_driveletters()


def postTest():
    get_driveletters()
    print("=" * 20)


def main():
    parsed_args = parse_arguments()

    print("Path:", parsed_args.path)

    p = PureWindowsPath(parsed_args.path)

    print("Path object:", p)

    print("Physical devices", get_DeviceId())

    if p.is_absolute():

        f1 = PureWindowsPath(p, "test01.dat")
        allocate_file(f1, 1024*1024*1024)

        with open(str(p.joinpath("winbtrfs.log")), "w") as log_file:

            preTest()
            ret = runWithPrint(["zpool", "create", "-f", "test01", tounc(f1)])
            time.sleep(10)
            if ret.returncode != 0:
                print("FAIL")
            print("Drive letters after pool create:", get_driveletters())
            time.sleep(10)
            postTest()

            for test in ['create', 'supersede', 'overwrite', 'open_id', 'io',
                         'mmap', 'rename', 'rename_ex', 'delete', 'delete_ex',
                         'links', 'links_ex', 'oplock_i', 'oplock_ii',
                         'oplock_batch', 'oplock_filter', 'oplock_r',
                         'oplock_rw', 'oplock_rh', 'oplock_rwh', 'cs',
                         'reparse', 'streams', 'fileinfo', 'ea']:
                preTest(str(test) + " tests:")
                f = PureWindowsPath(get_driveletters()[0][1])
                ret = runWithPrint([str(p.joinpath("winbtrfs", "test.exe")),
                                    str(test), str(f)])
                time.sleep(10)
                if ret.returncode != 0:
                    print("FAIL")
                postTest()

                print(ret.stdout.decode())

                out = " ".join([str(test),
                                ret.stdout.decode().splitlines()[-1]])

                print(out)
                log_file.write(out)
                log_file.write("\n")

            preTest()
#            runWithPrint(["zpool", "destroy", "-f", "test01"])
            time.sleep(10)
            postTest()

#            delete_file(f1)


if __name__ == "__main__":
    main()
