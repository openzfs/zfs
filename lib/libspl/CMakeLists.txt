
use_clang()

add_library(libspl
    atomic.c
    condvar.c
	cred.c
	kmem.c
	kstat.c
	libspl.c
    list.c
    mkdirp.c
    mutex.c
	procfs_list.c
	sid.c
	strlcat.c
	strlcpy.c
	random.c
	rwlock.c
	taskq.c
    thread.c
    timestamp.c
	tunables.c
	assert.c
	backtrace.c
    os/windows/posix.c
    #fdatasync.c
    #gethrestime.c
    #gethrtime.c
    #
	os/windows/getmntany.c
    os/windows/getopt.c
    #os/windows/getoptl.c
    os/windows/gethostid.c
	os/windows/page.c
    #os/windows/regex.c
    os/windows/signal.c
    #crc32.c
    os/windows/uio.c
    os/windows/xdr.c
    os/windows/xdr_array.c
    os/windows/xdr_float.c
    os/windows/xdr_mem.c
    #os/windows/zmount.c
    os/windows/zone.c
)

target_include_directories(libspl PUBLIC "${CMAKE_SOURCE_DIR}/include/os/windows/zfs")
target_link_libraries(libspl PUBLIC libpthread libregex PRIVATE advapi32 shell32)
