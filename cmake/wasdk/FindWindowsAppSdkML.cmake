find_package(CppWinRT)
# TODO: this could be optional, since it's only needed for
# framework-dependent deployments.
#
# Let's get something building first, so require it.
find_package(WindowsAppSdkRuntime REQUIRED)

set(WinMLPackageDirectory "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.WindowsAppSDK.ML")

find_path(WindowsAppSdkML_INCLUDE_DIR
    "onnxruntime_c_api.h"
    PATHS
    "${WinMLPackageDirectory}/include/winml"
    NO_DEFAULT_PATH
    REQUIRED)

find_path(WindowsAppSdkML_MODULE_DIR
    "Microsoft.Windows.AI.MachineLearning.dll"
    PATHS
    "${WinMLPackageDirectory}/runtimes-framework/win-x64/native/"
    NO_DEFAULT_PATH
    REQUIRED)

file(GLOB WindowsAppSdkML_WINMD_FILES
    LIST_DIRECTORIES false
    "${WinMLPackageDirectory}/metadata/*.winmd")
if (NOT WindowsAppSdkML_WINMD_FILES)
    message(SEND_ERROR "No .winmd files founder under '${WinMLPackageDirectory}/metadata/*.winmd'. This is unexpected, and this package will not work.")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WindowsAppSdkML
    REQUIRED_VARS
    WindowsAppSdkML_INCLUDE_DIR
    WindowsAppSdkML_MODULE_DIR
    WindowsAppSdkML_WINMD_FILES
    )

if(WindowsAppSdkML_FOUND AND NOT TARGET WindowsAppSdk::ML)
    # The calling application doesn't directly link with
    # Microsoft.Windows.AI.MachineLearning.dll, so make this an INTERFACE
    # library.
    #
    # Instead, WinRT loads this automatically when one of its types is
    # created inside the calling process via the C++/WinRT-generated code.
    add_library(WindowsAppSdk::ML INTERFACE IMPORTED)

    set_target_properties(WindowsAppSdk::ML
        PROPERTIES
        WINMD_FILES ${WindowsAppSdkML_WINMD_FILES}
        )
    target_include_directories(WindowsAppSdk::ML
        INTERFACE
        "${WindowsAppSdkML_INCLUDE_DIR}"
        )
endif()

if(WindowsAppSdkML_FOUND AND NOT TARGET WindowsAppSdk::ML::Bootstrap)
    add_library(WindowsAppSdk_ML_Bootstrap STATIC
        "${WinMLPackageDirectory}/include/WindowsMLAutoInitializer.cpp"
        )
    target_link_libraries(WindowsAppSdk_ML_Bootstrap
        PRIVATE
        WindowsAppSdk::ML
        WindowsAppSdk::Runtime
        )
    add_library(WindowsAppSdk::ML::Bootstrap ALIAS WindowsAppSdk_ML_Bootstrap)
endif()

if(WindowsAppSdkML_FOUND AND NOT TARGET WindowsAppSdk::ML::CodeGen)
    if (${CppWinRT_FOUND})
        get_target_property(WINMD_FILES WindowsAppSdk::ML WINMD_FILES)
        add_cppwinrt_codegen_target(WindowsAppSdk_ML_CodeGen
            INPUTS
            ${WINMD_FILES})
        add_library(WindowsAppSdk::ML::CodeGen ALIAS WindowsAppSdk_ML_CodeGen)
    endif()
endif()

# TODO: figure out the right way to express runtime dependenices. It's going
# to involve install(), I think.
#
# install(IMPORTED_RUNTIME_ARTIFACTS MicrosoftWindowsAppSDK::ML
#     RUNTIME
#     "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.WindowsAppSDK.ML/runtimes-framework/win-x64/native/DirectML.dll"
#     "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.WindowsAppSDK.ML/runtimes-framework/win-x64/native/onnxruntime.dll"
#     "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.WindowsAppSDK.ML/runtimes-framework/win-x64/native/onnxruntime_providers_shared.dll")
