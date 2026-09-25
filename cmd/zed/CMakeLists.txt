
use_clang()

um_add_executable(zed
	 zed.c
	 zed_conf.c
	 zed_disk_event.c
	 zed_event.c
	 zed_file.c
	 zed_log.c
	 zed_strings.c
	 agents/fmd_api.c
	 agents/fmd_serd.c
	 agents/zfs_agents.c
	 agents/zfs_diagnosis.c
	 agents/zfs_mod.c
	 agents/zfs_retire.c
	 os/windows/zed_exec.c
	 os/windows/resource.rc
)
target_link_libraries(zed PRIVATE
#	libnvpair
	libzfs
#	libzfs_core
	libzpool
)

target_include_directories(zed PRIVATE "${CMAKE_SOURCE_DIR}/cmd/zed" "${CMAKE_SOURCE_DIR}/include")

install(TARGETS zed RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zed>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)

