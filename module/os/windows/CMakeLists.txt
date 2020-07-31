
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

wdk_add_driver(OpenZFS
  debug.c
  driver.c
  Wpp.c
  OpenZFS_perf.rc
  resource.rc
)

target_link_libraries(OpenZFS PRIVATE
	splkern
	zlibkern
	icpkern
	luakern
	zfskern
	zfskern_os
	zcommonkern
	nvpairkern
	unicodekern
	# WDK libraries
	WDK::WDMSEC
	WDK::STORPORT
	WDK::SCSIWMI
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

add_custom_command(
	TARGET OpenZFS
	POST_BUILD
	BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
    COMMAND "${STAMPINF_PROGRAM}" -d "*" -a "amd64" -v "*" -f "${CMAKE_CURRENT_BINARY_DIR}\\driver\\OpenZFS.inf"
	COMMAND "${INF2CAT_PROGRAM}" /verbose "/driver:${CMAKE_CURRENT_BINARY_DIR}/driver" /os:10_19H1_X86,10_19H1_X64,ServerRS5_X64 /uselocaltime
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /t "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256  /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	# lundman's line
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
	VERBATIM COMMENT "Generating and signing .cat file"
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

execute_process(COMMAND  "${WDK_ROOT}/bin/${WDK_VERSION}/x86/ctrpp.exe"
  -o ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.h
  -rc ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.rc
  -ch ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_counters.h
 ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS.man )

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")

function(add_macro_property)
        foreach (I ${CSOURCES})
                get_filename_component(OUTPUT_FILE_WE ${I} NAME_WE)
                set_source_files_properties(${I} PROPERTIES COMPILE_FLAGS -DWPPFILE=${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh)
                message(STATUS "FILES_IN ======: ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh") 
        endforeach()
endfunction()

function(tracewpp OUTPUT_DIR SOURCE)
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
        set(DPRINT "dprintf{FLAGS=MYDRIVER_ALL_INFO, LEVEL=TRACE_INFO}(MSG, ...)")
        set(CFGDIR "${WPP_DIR}/wppconfig/rev1")
        set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
        set(WPP_MACRO "WPP_INLINE __inline")

        execute_process(COMMAND "${NATIVE_WPP_DIR}/${WDK_PLATFORM}/tracewpp.exe"
 -scan:${SCAN} /D${WPP_MACRO}
 -cfgdir:${CFGDIR} -I${CMAKE_CURRENT_BINARY_DIR} -odir:${NATIVE_OUTPUT_DIR} -km -func:${TRACE}
 -func:${DPRINT} -gen:{km-default.tpl}*.tmh ${NATIVE_SOURCE})

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
