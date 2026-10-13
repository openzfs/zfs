#! /bin/ksh -p
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
#

#
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

is_freebsd && ! python3 -c 'import sysctl' 2>/dev/null && log_unsupported "python3 sysctl module missing"

log_assert "zarcsummary generates output and doesn't return an error code"

# Without this, the below checks aren't going to work the way we hope...
set -o pipefail

for arg in "" "-a" "-d" "-p 1" "-g" "-s arc" "-r"; do
	log_must eval "zarcsummary $arg > /dev/null"
done

log_must eval "zarcsummary | head > /dev/null"
log_must eval "zarcsummary | head -1 > /dev/null"

#
# ksh's pipes are socketpairs, and zarcsummary writes unbuffered, a line
# at a time.  When head(1) starts late, zarcsummary fills the socket and
# blocks; head then exits with output unread, the socket is reset, and
# the blocked write fails with ECONNRESET rather than EPIPE.  The
# pipelines above only hit that by chance, so set it up directly: give
# zarcsummary a socket with the smallest send buffer, wait until it is
# blocked writing, then close the other end without reading.
#
if is_linux; then
	log_must python3 - zarcsummary <<-'EOF'
	import fcntl, socket, struct, subprocess, sys, termios, time

	r, w = socket.socketpair()
	w.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1)
	sndbuf = w.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
	p = subprocess.Popen(sys.argv[1:], stdout=w)

	def blocked():
	    outq = fcntl.ioctl(w, termios.TIOCOUTQ, b"\0" * 4)
	    with open("/proc/%d/stat" % p.pid) as f:
	        state = f.read().rpartition(")")[2].split()[0]
	    return struct.unpack("i", outq)[0] >= sndbuf and state == "S"

	deadline = time.monotonic() + 60
	while not blocked():
	    if p.poll() is not None:
	        sys.exit("zarcsummary exited before filling the socket")
	    if time.monotonic() > deadline:
	        sys.exit("zarcsummary did not block writing")
	    time.sleep(0.01)
	r.close()
	sys.exit(p.wait())
	EOF
fi

log_pass "zarcsummary generates output and doesn't return an error code"
