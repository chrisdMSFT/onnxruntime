find_program(CppWinRT_EXE_PATH
    cppwinrt.exe
    PATHS
    "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.Windows.CppWinRT/bin/"
    NO_DEFAULT_PATH
    REQUIRED)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CppWinRT
    REQUIRED_VARS
    CppWinRT_EXE_PATH
    )

if (CppWinRT_FOUND AND NOT TARGET CppWinRT::CppWinRT)  
    add_executable(CppWinRT::CppWinRT IMPORTED)
    set_target_properties(CppWinRT::CppWinRT
        PROPERTIES
        IMPORTED_LOCATION "${CppWinRT_EXE_PATH}"
        )

    function (add_cppwinrt_codegen_target TARGET)       
        set(OPTIONS)
        set(ONE_VALUE_KEYWORDS)
        set(MULTI_VALUE_KEYWORDS INPUTS)
        cmake_parse_arguments(
            PARSE_ARGV 1
            arg
            "${OPTIONS}"
            "${ONE_VALUE_KEYWORDS}"
            "${MULTI_VALUE_KEYWORDS}"
            )
        
        get_target_property(CPPWINRT_EXEC_PATH CppWinRT::CppWinRT IMPORTED_LOCATION)
        set(CPPWINRT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/${TARGET}/$<CONFIG>")
        set(CPPWINRT_RSP_FILE "${CPPWINRT_OUTPUT_DIR}/cppwinrt.rsp")
    
        set(cppwinrt_args "")
        list(APPEND cppwinrt_args "-input" "sdk+")
        foreach(winmd ${arg_INPUTS})
            list(APPEND cppwinrt_args "-input" "${winmd}")
        endforeach()
        list(APPEND cppwinrt_args "-base")
        list(APPEND cppwinrt_args "-optimize")
        list(APPEND cppwinrt_args "-output" "${CPPWINRT_OUTPUT_DIR}")
        list(APPEND cppwinrt_args "-verbose")
    
        add_custom_command(
            OUTPUT
            "${CPPWINRT_OUTPUT_DIR}/winrt/base.h"
            COMMAND
            "${CPPWINRT_EXEC_PATH}"
            ${cppwinrt_args}
            WORKING_DIRECTORY
            "${CPPWINRT_OUTPUT_DIR}"
            COMMENT
            "C++/WinRT codegen for ${TARGET}"
            DEPENDS
            # Re-run codegen if the codegen tool itself is updated.
            "${CPPWINRT_EXEC_PATH}"
            ${INPUTS}
            VERBATIM
            )

        # Dummy custom target so that the codegen command only runs if the
        # target is out of date.
        add_custom_target("${TARGET}_cppwinrt_codegen"
            DEPENDS
            "${CPPWINRT_OUTPUT_DIR}/winrt/base.h"
            )

        # 
        add_library("${TARGET}" INTERFACE)
        target_include_directories(
            "${TARGET}"
            SYSTEM INTERFACE
            "${CPPWINRT_OUTPUT_DIR}"
            )
        add_dependencies("${TARGET}" "${TARGET}_cppwinrt_codegen")
    endfunction()
endif()
