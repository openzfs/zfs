# https://apps.microsoft.com/store/detail/python-39/9P7QFQMJRFP7
# https://download.microsoft.com/download/7/9/6/
# 7962e9ce-cd69-4574-978c-1202654bd729/windowssdk/
# Installers/X64 Debuggers And Tools-x64_en-us.msi

import re
import subprocess

cdbstr = "C:\\Program Files\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe"
dumpfilestr = "C:\\Windows\\MEMORY.DMP"
symbolstr = "srv*;C:\\Program Files\\OpenZFS On Windows\\symbols\\;"


def run(arg):
    result = subprocess.run(
        [cdbstr, "-z", dumpfilestr, "-c", arg, "-y", symbolstr],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return re.search(
        r"Reading initial command[\s\S]+quit:", result.stdout.decode()
    ).group()


analyze = run("!analyze -v ; q")

stack = run("k ; q")

cbuf = run("dt OpenZFS!cbuf ; q")
# print(cbuf)
b = re.search(r"'dt OpenZFS!cbuf ; q'"
              "[\\s\\S]+?(0x[0-9A-Za-z]{8}`[0-9A-Za-z]{8})",
              cbuf)
cbufaddr = b.group()[-19:]

cbuf2 = run(
    ".writemem C:\\cbuf.txt " + cbufaddr + " L100000 ; q",
)

with open("C:\\stack.txt", "w") as file:
    file.write(analyze)
    file.write("\n")
    file.write(stack)


print("Please upload C:\\stack.txt and C:\\cbuf.txt "
      "when creating an issue on GitHub")
