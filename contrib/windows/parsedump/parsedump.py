# https://apps.microsoft.com/store/detail/python-39/9P7QFQMJRFP7
# https://download.microsoft.com/download/7/9/6/
# 7962e9ce-cd69-4574-978c-1202654bd729/windowssdk/
# Installers/X64 Debuggers And Tools-x64_en-us.msi

import re
import subprocess
import pathlib
import os
import winreg

pf64 = pathlib.WindowsPath(os.environ["ProgramFiles"])
pf86 = pathlib.WindowsPath(os.environ["ProgramFiles(x86)"])


def find_first_existing_file(file_paths):
    for file_path in file_paths:
        if file_path.exists():
            return file_path
    return None  # Return None if no file is found


def read_reg(path: pathlib.Path, hive=winreg.HKEY_LOCAL_MACHINE):
    try:
        with winreg.OpenKey(hive, str(path.parent)) as key:
            value, _ = winreg.QueryValueEx(key, path.name)
            return value
    except (OSError, FileNotFoundError):
        return None


def find_zfs():
    zfs_reg_path = (
        pathlib.Path("SOFTWARE")
        / "OpenZFS"
        / "OpenZFS On Windows"
        / "InstallLocation"
    )

    res = read_reg(zfs_reg_path)
    if not res:
        return None

    zfs_install_path = pathlib.WindowsPath(res)

    return zfs_install_path


# List of file paths to check
cdb_file_paths_to_check = [
    pf64 / "Windows Kits" / "10" / "Debuggers" / "x64" / "cdb.exe",
    pf86 / "Windows Kits" / "10" / "Debuggers" / "x64" / "cdb.exe",
    pf64 / "Windows Kits" / "10" / "Debuggers" / "x86" / "cdb.exe",
    pf86 / "Windows Kits" / "10" / "Debuggers" / "x86" / "cdb.exe"
]

cdb = find_first_existing_file(cdb_file_paths_to_check)

if cdb:
    print(cdb)
else:
    print("cdb not found.")
    exit()

zfs = find_zfs()

if zfs:
    print(zfs)
else:
    print("zfs not found.")
    exit()


dumpfilestr = "C:\\Windows\\MEMORY.DMP"
symbolstr = "srv*;" + str(zfs / "symbols") + "\\;"


def quote(string):
    return '"' + string + '"'


print(" ".join(["cdb command", quote(str(cdb)),
                "-z", quote(dumpfilestr),
                "-y", quote(symbolstr)]))


def run(arg):
    result = subprocess.run(
        [str(cdb), "-z", dumpfilestr, "-c", arg, "-y", symbolstr],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return re.search(
        r"Reading initial command[\s\S]+quit:", result.stdout.decode()
    ).group()


analyze = run("!analyze -v ; q")

stack = run("k ; q")

info = run(
    ";".join(
        [
            ".lines -e",
            ".kframes 100",
            ".echo ***** process info *****",
            "|",
            "!peb",
            "!analyze -vp",
            ".ecxr",
            "kp",
            "!uniqstack -vp",
            "!gle -all",
            "q",
        ]
    )
)

cbuf = run(".writemem C:\\cbuf.txt poi(OpenZFS!cbuf) L100000 ; q")

with open("C:\\stack.txt", "w") as file:
    file.write(analyze)
    file.write("\n")
    file.write(stack)

with open("C:\\info.txt", "w") as file:
    file.write(info)

print("Please upload C:\\stack.txt, C:\\info.txt and C:\\cbuf.txt "
      "when creating an issue on GitHub")
