
use_clang()

um_add_executable(zed_service
	zed_service.c
	ops_status.c
	ops_import.c
	ops_export.c
	ops_crypto.c
	ops_mount.c
	ops_common.c
	# resource.rc
)

target_include_directories(zed_service PRIVATE
	"${CMAKE_SOURCE_DIR}/cmd/os/windows/zed_service"
	# "${CMAKE_SOURCE_DIR}/lib/libspl/include/os/windows" # soon
	# "${CMAKE_SOURCE_DIR}/lib/libspl/include" # soon
	# "${CMAKE_SOURCE_DIR}/lib/os/windows" # maybe
	"${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(zed_service PRIVATE
	advapi32
	libnvpair
	libspl
	libzpool
	libzfs
	# libspl # soon
)

install(TARGETS zed_service RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zed_service>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)

