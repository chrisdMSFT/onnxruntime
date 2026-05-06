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

#include <windows.h>
#include <WinMLEpCatalog.h>

#ifndef BUILD_WINML_STANDALONE_PERF_TEST
#error "This file should only be compiled when BUILD_WINML_STANDALONE_PERF_TEST is ON"
#endif

static WinMLEpCatalogHandle g_ep_catalog = nullptr;
static std::vector<std::string> g_registered_providers;

extern "C" const OrtApiBase* __cdecl OrtGetApiBase() noexcept
{
    static const OrtApiBase* s_ortApiBase = []() -> const OrtApiBase* {
        // Load onnxruntime.dll from EXE directory
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

const char* ready_state_to_string(WinMLEpReadyState state)
{
    switch (state)
    {
    case WinMLEpReadyState_Ready: return "Ready";
    case WinMLEpReadyState_NotReady: return "NotReady";
    case WinMLEpReadyState_NotPresent: return "NotPresent";
    default: return "Unknown";
    }
}

void WinML_FindAndRegisterAllProviders(Ort::Env& env, const std::vector<std::string>& provider_filter)
{
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

    WinMLEpCatalogEnumProviders(catalog, [](WinMLEpHandle ep, const WinMLEpInfo* info, void* context) -> BOOL {
        auto* ctx = static_cast<EnumContext*>(context);
        std::string providerName = info->name ? info->name : "";
        std::cout << "[WinML Standalone] Found provider: " << providerName << std::endl;

        // If filter is not empty, only register providers that match
        if (!ctx->filter->empty())
        {
            auto it = std::find(ctx->filter->begin(), ctx->filter->end(), providerName);
            if (it == ctx->filter->end())
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

        // Get library path and register
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
            std::cerr << "  Failed to get library path" << std::endl;
            return TRUE;
        }
        libPath.resize(used > 0 ? used - 1 : 0);  // trim null terminator

        std::cout << "  Registering from: " << libPath << std::endl;

        // Convert to wide string for RegisterExecutionProviderLibrary
        std::wstring wpath(libPath.begin(), libPath.end());

        try
        {
            ctx->env->RegisterExecutionProviderLibrary(providerName.c_str(), wpath);
            ctx->registered->push_back(providerName);
            std::cout << "  Registered successfully" << std::endl;
        }
        catch (const Ort::Exception& e)
        {
            std::cerr << "  Registration failed: " << e.what() << std::endl;
        }

        return TRUE;
    }, &ctx);
}

void WinML_InitializeAndRegisterAllProviders(Ort::Env& env, const std::vector<std::string>& provider_filter)
{
    WinML_FindAndRegisterAllProviders(env, provider_filter);
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
