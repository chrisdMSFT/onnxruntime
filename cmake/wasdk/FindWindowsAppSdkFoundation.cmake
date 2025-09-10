set(WinAppSdkFoundationPackageDirectory "${CMAKE_CURRENT_LIST_DIR}/../../packages/nuget/Microsoft.WindowsAppSDK.Foundation")

find_library(WinAppSdkFoundation_Bootstrap_LIBRARY
    "Microsoft.WindowsAppRuntime.Bootstrap"
    PATHS
    "${WinAppSdkFoundationPackageDirectory}/lib/native/x64"
    NO_DEFAULT_PATH
    REQUIRED)

find_file(WinAppSdkFoundation_Bootstrap_RUNTIME_DLL
    "Microsoft.WindowsAppRuntime.Bootstrap.dll"
    PATHS
    "${WinAppSdkFoundationPackageDirectory}/runtimes/win-x64/native"
    NO_DEFAULT_PATH
    REQUIRED)

find_path(WinAppSdkFoundation_INCLUDE_DIR
    "MddBootstrap.h"
    PATHS
    "${WinAppSdkFoundationPackageDirectory}/include"
    NO_DEFAULT_PATH
    REQUIRED)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WindowsAppSdkFoundation
    REQUIRED_VARS
    WinAppSdkFoundation_Bootstrap_LIBRARY
    WinAppSdkFoundation_Bootstrap_RUNTIME_DLL
    WinAppSdkFoundation_INCLUDE_DIR
    )

if (WindowsAppSdkFoundation_FOUND AND NOT TARGET WindowsAppSdk::Bootstrap)
    add_library(WindowsAppSdk::Bootstrap SHARED IMPORTED)
    set_target_properties(WindowsAppSdk::Bootstrap 
        PROPERTIES
        IMPORTED_IMPLIB "${WinAppSdkFoundation_Bootstrap_LIBRARY}"
        IMPORTED_LOCATION "${WinAppSdkFoundation_Bootstrap_RUNTIME_DLL}"
        RUNTIME_DLLS "${WinAppSdkFoundation_Bootstrap_RUNTIME_DLL}"
        )
    target_include_directories(WindowsAppSdk::Bootstrap
        INTERFACE
        "${WinAppSdkFoundation_INCLUDE_DIR}"
        )
    # Ensure consumers have the Windows App SDK bootstrap auto-init macro defined
    target_compile_definitions(WindowsAppSdk::Bootstrap
        INTERFACE
        "MICROSOFT_WINDOWSAPPSDK_AUTOINITIALIZE_BOOTSTRAP=1"
    )
endif()
