
use_clang()

wdk_add_library(splkern
  spl-atomic.c
  spl-condvar.c
  spl-cred.c
  spl-ddi.c
  spl-debug.c
  spl-err.c
  spl-kmem.c
  spl-kstat.c
  spl-list.c
  spl-md5.c
  spl-mount.c
  spl-mutex.c
  spl-policy.c
  spl-proc.c
  spl-processor.c
  spl-proc_list.c
  spl-rwlock.c
  spl-seg_kmem.c
  spl-taskq.c
  spl-thread.c
  spl-time.c
  spl-tsd.c
  spl-uio.c
  spl-vmem.c
  spl-vnode.c
  spl-windows.c
  spl-xdr.c
  spl-lookasidelist.c
)

# set(CMAKE_TOOLCHAIN_FILE $ENV{CMAKE_TOOLCHAIN_FILE})

target_include_directories(splkern BEFORE PUBLIC "${CMAKE_SOURCE_DIR}/include/os/windows/" PUBLIC "${CMAKE_SOURCE_DIR}/include/os/windows/spl")

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")

list(REMOVE_ITEM CSOURCES "${CMAKE_CURRENT_SOURCE_DIR}/spl-wmsum.c")

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
