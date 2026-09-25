
use_clang()

um_add_executable(zstreamdump
	zstream.c
	zstream_backtrace.c
	zstream_byteswap.c
	zstream_chain.c
	zstream_decompress.c
	zstream_drop_record.c
	zstream_dump.c
	zstream_fletcher4.c
	zstream_io.c
	zstream_queue.c
	zstream_raw.c
	zstream_recompress.c
	zstream_redup.c
	zstream_selftest.c
	zstream_selftest_queue.c
	zstream_token.c
	zstream_util.c
	zstream_validate.c
	os/windows/resource.rc
)

target_link_libraries(zstreamdump PRIVATE
	libnvpair
	libuuid
	libzfs
	libzfs_core
	libzpool
)
install(TARGETS zstreamdump RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zstreamdump>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
