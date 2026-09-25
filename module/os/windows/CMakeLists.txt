
#include(ExternalProject)

#ExternalProject_Add ( spl
#    PREFIX "spl"
#    CONFIGURE_COMMAND ${CMAKE_COMMAND} 
#    SOURCE_DIR "spl" # Tell CMake to use subdirectory as source.
#)

#ExternalProject_Add ( zfs
#    PREFIX "zfs"
#    CONFIGURE_COMMAND ${CMAKE_COMMAND} 
#    SOURCE_DIR "zfs" # Tell CMake to use subdirectory as source.
#)

add_subdirectory(spl)
add_subdirectory(zfs)
add_subdirectory(zvol)

string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}")
string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

wdk_add_driver(OpenZFS
  debug.c
  driver.c
  Wpp.c
  OpenZFS_perf.rc
  resource.rc
)

target_link_libraries(OpenZFS
	splkern
	zlibkern
	icpkern
	luakern
	zfskern
	zfskern_os
	zcommonkern
	nvpairkern
	# WDK libraries
	WDK::WDMSEC
#	WDK::STORPORT
#	WDK::SCSIWMI
)

# set(CMAKE_LINKER link.exe)

set_target_properties(OpenZFS
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
)

configure_file(OpenZFS.inf "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.inf" COPYONLY)
configure_file(OpenZFS.man "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.man" COPYONLY)

set(OPENZFS_CAT_SIGN_COMMANDS "")
if (OPENZFS_SIGNTOOL_SHA1)
	list(APPEND OPENZFS_CAT_SIGN_COMMANDS
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	)
endif()

add_custom_command(
	TARGET OpenZFS
	POST_BUILD
	BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
    COMMAND "${STAMPINF_PROGRAM}" -d "*" -v "*" -f "${CMAKE_CURRENT_BINARY_DIR}\\driver\\OpenZFS.inf"
	COMMAND "${INF2CAT_PROGRAM}" /verbose "/driver:${CMAKE_CURRENT_BINARY_DIR}/driver" /os:10_19H1_X86,10_19H1_X64,10_19H1_ARM64,ServerRS5_X64 /uselocaltime
	${OPENZFS_CAT_SIGN_COMMANDS}
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
	VERBATIM COMMENT "Generating and signing OpenZFS.cat file"
)

install(TARGETS OpenZFS RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/driver")
install(FILES 
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.inf"
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
    "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.man"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/driver"
)

install(FILES
	$<TARGET_PDB_FILE:OpenZFS>
	DESTINATION "${CMAKE_INSTALL_BINDIR}/driver"
	OPTIONAL
)

find_program(CTRPP_PROGRAM
	ctrpp
	HINTS
		"${WDK_ROOT}/bin/${WDK_VERSION}/x86"
		"$ENV{WINSDK_NUGET_ROOT}/bin/${WDK_VERSION}/x86"
	NO_DEFAULT_PATH
)

if (NOT CTRPP_PROGRAM)
	message(FATAL_ERROR "Failed to find ctrpp.exe")
endif()

execute_process(
	COMMAND "${CTRPP_PROGRAM}"
		-o "${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.h"
		-rc "${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.rc"
		-ch "${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_counters.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS.man"
	RESULT_VARIABLE CTRPP_RESULT
	OUTPUT_VARIABLE CTRPP_STDOUT
	ERROR_VARIABLE CTRPP_STDERR
)

if (NOT CTRPP_RESULT EQUAL 0)
	message(FATAL_ERROR
		"ctrpp failed with exit code ${CTRPP_RESULT}\n"
		"stdout: ${CTRPP_STDOUT}\n"
		"stderr: ${CTRPP_STDERR}")
endif()

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")

function(add_macro_property)
        foreach (I ${CSOURCES})
                get_filename_component(OUTPUT_FILE_WE ${I} NAME_WE)
                set_source_files_properties(${I} PROPERTIES COMPILE_FLAGS -DWPPFILE=${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh)
                message(STATUS "FILES_IN ======: ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh") 
        endforeach()
endfunction()

function(XXXtracewpp OUTPUT_DIR SOURCE)
        # [mlr] list of .tmh files to be generated -> TMH
        set(WPP_DIR "${WDK_ROOT}/bin/${WDK_VERSION}")

        get_filename_component(FILEN ${SOURCE} NAME)
        set(TMH_FILEN ${FILEN}.tmh)
        set(TMH ${OUTPUT_DIR}/${TMH_FILEN})
        set(EXTENSIONS ".c")

        # [mlr] cmake only converts the command name to the native path format. the
        # path names to be used in arguments must be converted manually.

        file(TO_NATIVE_PATH ${SOURCE} NATIVE_SOURCE)
        file(TO_NATIVE_PATH ${WPP_DIR} NATIVE_WPP_DIR)
        file(TO_NATIVE_PATH ${OUTPUT_DIR} NATIVE_OUTPUT_DIR)

        # [mlr] note that if -preserveext: occurs after the source file specification, it has
        # no effect.

        set(TRACE "TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...)")
        set(CFGDIR "${WPP_DIR}/wppconfig/rev1")
        set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
        set(WPP_MACRO "WPP_INLINE __inline")

        execute_process(COMMAND "${NATIVE_WPP_DIR}/${WDK_PLATFORM}/tracewpp.exe"
 -scan:${SCAN} /D${WPP_MACRO}
 -cfgdir:${CFGDIR} -I${CMAKE_CURRENT_BINARY_DIR} -odir:${NATIVE_OUTPUT_DIR} -km -func:${TRACE}
 -gen:{km-default.tpl}*.tmh ${NATIVE_SOURCE})

endfunction()


function(wpp OUTPUT_DIR)

        add_macro_property()
        # [mlr] invoke tracewpp() for each source file, adding the resulting file to a list
        # named TMH.
        message(STATUS "OUTPUT_DIR ======: ${OUTPUT_DIR}")

        foreach ( I ${CSOURCES} )
                tracewpp(${OUTPUT_DIR} ${I})
        endforeach()

endfunction()

wpp("${CMAKE_SOURCE_DIR}/out/build")
