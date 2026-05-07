// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#include "winml_standalone.h"

#include <core/session/onnxruntime_c_api.h>
#include <core/session/onnxruntime_cxx_api.h>

#include <string>
#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>

#include <windows.h>
#include <WinMLEpCatalog.h>

#ifndef BUILD_WINML_STANDALONE_PERF_TEST
#error "This file should only be compiled when BUILD_WINML_STANDALONE_PERF_TEST is ON"
#endif

static WinMLEpCatalogHandle g_ep_catalog = nullptr;
static std::vector<std::string> g_registered_providers;

// ===========================================================================
// LINK-ORDER WARNING:
//
// This translation unit redefines `OrtGetApiBase` to dynamically load
// onnxruntime.dll from the EXE directory rather than statically link to the
// import lib. This relies on no other static lib in the link line for the
// `winml_standalone_perf_test` target ever pulling in a definition of
// `OrtGetApiBase`. If a future change introduces such a definition, the
// linker will report a duplicate-symbol error pointing at this file.
//
// (Today the standalone target links onnx_test_runner_common,
// onnxruntime_test_utils, onnxruntime_common, onnxruntime_flatbuffers, and
// onnx_test_data_proto -- none of those bring in OrtGetApiBase.)
// ===========================================================================
extern "C" const OrtApiBase* __cdecl OrtGetApiBase() noexcept
{
    static const OrtApiBase* s_ortApiBase = []() -> const OrtApiBase* {
        // Resolve onnxruntime.dll exclusively from the EXE directory and
        // ignore PATH / SetDllDirectory. This is intentional: the bundled
        // WinML NuGet package is the contract owner of the runtime version,
        // so we want to refuse to silently pick up a different sideloaded
        // onnxruntime.dll.
        wchar_t exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return nullptr;

        std::filesystem::path ortPath = std::filesystem::path(exePath).parent_path() / L"onnxruntime.dll";
        HMODULE onnxruntimeModule = LoadLibraryExW(ortPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!onnxruntimeModule)
        {
            std::wcerr << L"[WinML Standalone] Failed to load: " << ortPath.c_str() << std::endl;
            return nullptr;
        }

        std::wcout << L"[WinML Standalone] Loaded: " << ortPath.c_str() << std::endl;

        using OrtGetApiBaseFunc = const OrtApiBase* (*)();
        auto ortGetApiBase = reinterpret_cast<OrtGetApiBaseFunc>(GetProcAddress(onnxruntimeModule, "OrtGetApiBase"));
        if (ortGetApiBase)
            return ortGetApiBase();

        return nullptr;
    }();

    return s_ortApiBase;
}

void WinML_InitializeAndRegisterAllProviders(Ort::Env& env, const std::vector<std::string>& provider_filter)
{
    // Must be called exactly once per process before WinML_Uninitialize.
    // Re-init without an intervening uninit would leak the previous catalog
    // handle and silently append to g_registered_providers.
    assert(!g_ep_catalog && "WinML_InitializeAndRegisterAllProviders called twice without WinML_Uninitialize");

    std::cout << "[WinML Standalone] Discovering and registering EPs..." << std::endl;

    WinMLEpCatalogHandle catalog = nullptr;
    HRESULT hr = WinMLEpCatalogCreate(&catalog);
    if (FAILED(hr))
    {
        std::cerr << "[WinML Standalone] ERROR: WinMLEpCatalogCreate failed (0x"
                  << std::hex << hr << std::dec << ")" << std::endl;
        throw std::runtime_error("WinMLEpCatalogCreate failed");
    }

    g_ep_catalog = catalog;

    struct EnumContext {
        const std::vector<std::string>* filter;
        Ort::Env* env;
        std::vector<std::string>* registered;
    };
    EnumContext ctx{ &provider_filter, &env, &g_registered_providers };

    HRESULT enumHr = WinMLEpCatalogEnumProviders(catalog,
        [](WinMLEpHandle ep, const WinMLEpInfo* info, void* context) -> BOOL {
            auto* ec = static_cast<EnumContext*>(context);
            std::string providerName = info->name ? info->name : "";
            std::cout << "[WinML Standalone] Found provider: " << providerName << std::endl;

            // If filter is not empty, only register providers that match
            if (!ec->filter->empty())
            {
                auto it = std::find(ec->filter->begin(), ec->filter->end(), providerName);
                if (it == ec->filter->end())
                {
                    std::cout << "  Skipping (not in filter list)" << std::endl;
                    return TRUE;
                }
            }

            // Check ready state
            WinMLEpReadyState state;
            HRESULT stateHr = WinMLEpGetReadyState(ep, &state);
            if (FAILED(stateHr))
            {
                std::cerr << "  Failed to get ready state (0x" << std::hex << stateHr << std::dec << ")" << std::endl;
                return TRUE;
            }

            if (state == WinMLEpReadyState_NotPresent)
            {
                std::cout << "  Skipping " << providerName << " (NotPresent)" << std::endl;
                return TRUE;
            }

            if (state != WinMLEpReadyState_Ready)
            {
                HRESULT readyHr = WinMLEpEnsureReady(ep);
                if (FAILED(readyHr))
                {
                    std::cerr << "  EnsureReady failed (0x" << std::hex << readyHr << std::dec << ")" << std::endl;
                    return TRUE;
                }
                stateHr = WinMLEpGetReadyState(ep, &state);
                if (FAILED(stateHr) || state != WinMLEpReadyState_Ready)
                {
                    std::cerr << "  Failed to become Ready after EnsureReady" << std::endl;
                    return TRUE;
                }
            }

            std::cout << "  " << providerName << " is Ready" << std::endl;

            // WinMLEpCatalogApi.cpp contract (verified):
            //   pathSize = utf8Path.size() + 1  (UTF-8, includes NUL)
            //   used     = pathSize on success
            // We use strnlen rather than `used - 1` so a future runtime that
            // changes whether the count includes the NUL still produces the
            // correct length.
            size_t pathSize = 0;
            HRESULT pathSizeHr = WinMLEpGetLibraryPathSize(ep, &pathSize);
            if (FAILED(pathSizeHr) || pathSize == 0)
            {
                std::cerr << "  Failed to get library path size" << std::endl;
                return TRUE;
            }

            std::string libPath(pathSize, '\0');
            size_t used = 0;
            HRESULT pathHr = WinMLEpGetLibraryPath(ep, pathSize, libPath.data(), &used);
            if (FAILED(pathHr))
            {
                std::cerr << "  Failed to get library path (0x"
                          << std::hex << pathHr << std::dec << ")" << std::endl;
                return TRUE;
            }
            if (used > pathSize)
            {
                std::cerr << "  WinMLEpGetLibraryPath returned used=" << used
                          << " > buffer size=" << pathSize << " (contract violation)" << std::endl;
                return TRUE;
            }
            libPath.resize(strnlen(libPath.data(), pathSize));

            std::cout << "  Registering from: " << libPath << std::endl;

            // Convert UTF-8 path to UTF-16 for RegisterExecutionProviderLibrary.
            // The two-iterator std::wstring(begin, end) ctor only round-trips
            // ASCII (it zero-extends each char to wchar_t), which silently
            // corrupts any non-ASCII bytes (e.g. localized user folders).
            std::wstring wpath;
            if (!libPath.empty())
            {
                int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            libPath.data(), static_cast<int>(libPath.size()),
                                            nullptr, 0);
                if (n <= 0)
                {
                    std::cerr << "  Failed to convert library path to UTF-16 (GetLastError=0x"
                              << std::hex << GetLastError() << std::dec << ")" << std::endl;
                    return TRUE;
                }
                wpath.resize(static_cast<size_t>(n));
                int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                  libPath.data(), static_cast<int>(libPath.size()),
                                                  wpath.data(), n);
                if (written != n)
                {
                    std::cerr << "  UTF-8 to UTF-16 conversion size mismatch" << std::endl;
                    return TRUE;
                }
            }

            try
            {
                ec->env->RegisterExecutionProviderLibrary(providerName.c_str(), wpath);
                ec->registered->push_back(providerName);
                std::cout << "  Registered successfully" << std::endl;
            }
            catch (const Ort::Exception& e)
            {
                std::cerr << "  Registration failed: " << e.what() << std::endl;
            }

            return TRUE;
        }, &ctx);

    if (FAILED(enumHr))
    {
        std::cerr << "[WinML Standalone] ERROR: WinMLEpCatalogEnumProviders failed (0x"
                  << std::hex << enumHr << std::dec << ")" << std::endl;
        throw std::runtime_error("WinMLEpCatalogEnumProviders failed");
    }

    // If the caller asked for specific providers, fail loudly when any
    // requested one was not registered. Otherwise the perf test would
    // silently fall back to CPU-only and the real failure would be buried
    // in stderr.
    if (!provider_filter.empty())
    {
        std::vector<std::string> missing;
        for (const auto& requested : provider_filter)
        {
            if (std::find(g_registered_providers.begin(), g_registered_providers.end(), requested)
                == g_registered_providers.end())
            {
                missing.push_back(requested);
            }
        }
        if (!missing.empty())
        {
            std::string msg = "[WinML Standalone] ERROR: Requested provider(s) not registered:";
            for (const auto& name : missing) { msg += ' '; msg += name; }
            std::cerr << msg << std::endl;
            throw std::runtime_error(msg);
        }
    }
}

void WinML_Uninitialize(Ort::Env& env)
{
    // Unregister EP libraries
    for (const auto& path : g_registered_providers)
    {
        try
        {
            env.UnregisterExecutionProviderLibrary(path.c_str());
        }
        catch (...)
        {
            // Best-effort cleanup
        }
    }
    g_registered_providers.clear();

    // Release the EP catalog
    if (g_ep_catalog)
    {
        WinMLEpCatalogRelease(g_ep_catalog);
        g_ep_catalog = nullptr;
    }
}
