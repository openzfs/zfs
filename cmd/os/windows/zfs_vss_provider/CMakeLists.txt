
use_clang()

#
# Out-of-process COM server EXE registered as LocalServer32.
# VSS activates software providers via CLSCTX_LOCAL_SERVER; COM routes
# the request to our running Windows service (AppID/LocalService).
# The coordinator then uses the VSS LRPC channel — not generic DCOM —
# for all provider calls, enabling proper VSS_OBJECT_PROP serialisation
# (copySnapshot) that is absent with the DllSurrogate approach.
#
um_add_executable(zfs_vss_provider
	vss_service.c
	vss_registry.c
	vss_provider.cpp
)

target_include_directories(zfs_vss_provider PRIVATE
	"${CMAKE_SOURCE_DIR}/cmd/os/windows/zfs_vss_provider"
	"${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(zfs_vss_provider PRIVATE
	advapi32
	ole32
	oleaut32
	libnvpair
	libspl
	libzpool
	libzfs
)

if (OPENZFS_SIGNTOOL_SHA1)
	add_custom_command(
		TARGET zfs_vss_provider
		POST_BUILD
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256
			/sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}"
			$<TARGET_FILE:zfs_vss_provider>
		WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		VERBATIM COMMENT "Sign VSS provider EXE"
	)
endif()

install(TARGETS zfs_vss_provider RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zfs_vss_provider>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
