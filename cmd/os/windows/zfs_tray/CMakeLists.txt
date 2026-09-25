
use_clang()

um_add_executable(zfs_tray WIN32
	tray.c
	rpc_client.c
	pool_json.c
	import_window.c
	pass_prompt.c
	resource.rc
)

target_include_directories(zfs_tray PRIVATE
	"${CMAKE_SOURCE_DIR}/cmd/os/windows/zed_service"
	"${CMAKE_SOURCE_DIR}/cmd/os/windows/zfs_tray"
	# "${CMAKE_SOURCE_DIR}/lib/libspl/include/os/windows" # soon
	# "${CMAKE_SOURCE_DIR}/lib/libspl/include" # soon
	# "${CMAKE_SOURCE_DIR}/lib/os/windows" # maybe
	# "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(zfs_tray PRIVATE
	user32
	shell32
	comctl32
)

install(TARGETS zfs_tray RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zfs_tray>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)

target_link_options(zfs_tray PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
