// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifdef BUILD_STANDALONE_WINML_PERF_TEST

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "test/perftest/windows/standalone_winml_bootstrap.h"

#include <core/session/onnxruntime_c_api.h>
#include <core/session/onnxruntime_cxx_api.h>
#include <core/common/path_string.h>

#include <WinMLEpCatalog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

// Process-scoped state. The standalone_winml_perf_test binary runs once per
// invocation; the bootstrap is called once after env construction and once
// before env destruction. NOT thread-safe — callers must not invoke
// Register/Unregister concurrently.
WinMLEpCatalogHandle g_catalog = nullptr;
std::vector<std::string> g_registered_eps;

// Captured during enumeration so EnsureReady (potentially long-running)
// happens outside the catalog's enum iterator.
struct EnumCaptured {
  WinMLEpHandle handle;
  std::string name_from_info;
  WinMLEpReadyState ready_state_from_info;
  WinMLEpCertification certification_from_info;
  std::string library_path_from_info;
};

const char* ReadyStateString(WinMLEpReadyState state) {
  switch (state) {
    case WinMLEpReadyState_Ready:      return "Ready";
    case WinMLEpReadyState_NotReady:   return "NotReady";
    case WinMLEpReadyState_NotPresent: return "NotPresent";
    default:                           return "Unknown";
  }
}

const char* CertificationString(WinMLEpCertification cert) {
  switch (cert) {
    case WinMLEpCertification_Unknown:     return "Unknown";
    case WinMLEpCertification_Certified:   return "Certified";
    case WinMLEpCertification_Uncertified: return "Uncertified";
    default:                               return "?";
  }
}

std::string QueryEpName(WinMLEpHandle ep) {
  size_t needed = 0;
  HRESULT hr = ::WinMLEpGetNameSize(ep, &needed);
  if (FAILED(hr) || needed == 0) {
    return {};
  }
  std::vector<char> buf(needed);
  size_t used = 0;
  hr = ::WinMLEpGetName(ep, buf.size(), buf.data(), &used);
  if (FAILED(hr) || used == 0) {
    return {};
  }
  if (buf[used - 1] == '\0') {
    --used;
  }
  return std::string(buf.data(), used);
}

std::string QueryEpLibraryPath(WinMLEpHandle ep) {
  size_t needed = 0;
  HRESULT hr = ::WinMLEpGetLibraryPathSize(ep, &needed);
  if (FAILED(hr) || needed == 0) {
    return {};
  }
  std::vector<char> buf(needed);
  size_t used = 0;
  hr = ::WinMLEpGetLibraryPath(ep, buf.size(), buf.data(), &used);
  if (FAILED(hr) || used == 0) {
    return {};
  }
  if (buf[used - 1] == '\0') {
    --used;
  }
  return std::string(buf.data(), used);
}

WinMLEpReadyState QueryEpReadyState(WinMLEpHandle ep) {
  WinMLEpReadyState state = WinMLEpReadyState_NotPresent;
  ::WinMLEpGetReadyState(ep, &state);  // best-effort
  return state;
}

bool IsBlacklisted(const std::string& ep_name) {
  // Populate as needed during validation (e.g. EP names known to crash
  // when loaded against locally-built ORT). Empty by default.
  static constexpr std::initializer_list<const char*> kBlacklist = {};
  for (const char* name : kBlacklist) {
    if (ep_name == name) return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// Path helpers for the OrtGetApiBase shim.

// Returns the host exe's full path. Handles both MAX_PATH-fitting and
// long paths transparently. Returns empty string on failure.
std::wstring GetHostExePath() {
  // First try a stack buffer sized for typical paths.
  wchar_t buf[MAX_PATH] = {0};
  DWORD got = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (got > 0 && got < MAX_PATH) {
    return std::wstring(buf, got);
  }
  // Long path or error. Try a generously-sized buffer.
  std::wstring big(32768, L'\0');
  got = ::GetModuleFileNameW(nullptr, &big[0], static_cast<DWORD>(big.size()));
  if (got == 0 || got >= big.size()) {
    return {};
  }
  big.resize(got);
  return big;
}

// Returns parent directory of `path`, or empty string if no separator found.
std::wstring ParentDirectoryOf(const std::wstring& path) {
  if (path.empty()) return {};
  // Last '\' or '/' — pointer comparison would be UB across nullptrs, so
  // handle null/non-null cases explicitly.
  size_t back = path.find_last_of(L'\\');
  size_t fwd = path.find_last_of(L'/');
  size_t pos;
  if (back == std::wstring::npos && fwd == std::wstring::npos) {
    return {};
  } else if (back == std::wstring::npos) {
    pos = fwd;
  } else if (fwd == std::wstring::npos) {
    pos = back;
  } else {
    pos = (back > fwd) ? back : fwd;
  }
  return path.substr(0, pos);
}

[[noreturn]] void FatalShimError(const wchar_t* msg, const wchar_t* extra = L"", DWORD err = 0) {
  ::fwprintf(stderr,
             L"[StandaloneWinML] FATAL: OrtGetApiBase shim — %ls %ls (GetLastError=%lu)\n",
             msg, extra, err);
  ::fflush(stderr);
  // Hard fail: returning nullptr would crash the very next instruction in
  // main.cc:27 (`OrtGetApiBase()->GetApi(...)`). Better to abort cleanly with
  // a diagnostic message.
  std::abort();
}

const OrtApiBase* LoadOrtApiBaseFromExeDir() {
  const std::wstring exe_path = GetHostExePath();
  if (exe_path.empty()) {
    FatalShimError(L"GetModuleFileNameW failed", L"", ::GetLastError());
  }

  const std::wstring parent = ParentDirectoryOf(exe_path);
  if (parent.empty()) {
    FatalShimError(L"no path separator in exe path", exe_path.c_str());
  }

  const std::wstring dll_path = parent + L"\\onnxruntime.dll";

  // LOAD_WITH_ALTERED_SEARCH_PATH: use the directory of `dll_path` as the
  // base of the dependency search. Combined with an absolute path this
  // ensures the locally-staged DLL's own dependencies (e.g. providers_shared)
  // are resolved from the same directory rather than from the system PATH.
  HMODULE mod = ::LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!mod) {
    FatalShimError(L"LoadLibraryExW failed for", dll_path.c_str(), ::GetLastError());
  }

  using OrtGetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)(void);
  auto fn = reinterpret_cast<OrtGetApiBaseFn>(::GetProcAddress(mod, "OrtGetApiBase"));
  if (!fn) {
    FatalShimError(L"GetProcAddress(OrtGetApiBase) failed in", dll_path.c_str(), ::GetLastError());
  }

  const OrtApiBase* api_base = fn();
  if (!api_base) {
    FatalShimError(L"OrtGetApiBase returned nullptr from", dll_path.c_str());
  }

  // Module is intentionally never freed: the returned OrtApi* and any later
  // OrtEnv / sessions / EP-registered libraries reference code inside it.
  // FreeLibrary while any of them is alive would crash on next use.
  return api_base;
}

// Erase from `vec` all entries that appear in `to_remove` (preserves order
// of remaining entries; O(n*m) but n is tiny here).
void EraseAll(std::vector<std::string>& vec, const std::vector<std::string>& to_remove) {
  if (to_remove.empty() || vec.empty()) return;
  vec.erase(std::remove_if(vec.begin(), vec.end(),
                           [&](const std::string& s) {
                             return std::find(to_remove.begin(), to_remove.end(), s) != to_remove.end();
                           }),
            vec.end());
}

}  // namespace

// -----------------------------------------------------------------------------
// OrtGetApiBase shim
//
// Resolves <exe_dir>\onnxruntime.dll (the locally-built ORT staged sibling to
// the exe by cmake/standalone_winml_perf_test.cmake POST_BUILD) and forwards
// to its OrtGetApiBase symbol. The static-init lambda is C++11 thread-safe
// (Magic Statics, [stmt.dcl]/4). On any failure the shim aborts the process
// with a diagnostic — returning nullptr would crash the caller (main.cc:27)
// without explanation.

extern "C" const OrtApiBase* ORT_API_CALL OrtGetApiBase(void) NO_EXCEPTION {
  static const OrtApiBase* const s_api_base = LoadOrtApiBaseFromExeDir();
  return s_api_base;
}

// -----------------------------------------------------------------------------
// EP registration

namespace onnxruntime {
namespace perftest {

namespace {

BOOL CALLBACK EnumProvidersCallback(WinMLEpHandle ep, const WinMLEpInfo* info, void* context) {
  if (!info || !context) {
    return TRUE;  // continue enumeration
  }
  auto* out = static_cast<std::vector<EnumCaptured>*>(context);
  EnumCaptured cap{};
  cap.handle = ep;
  // Defensive copy: WinMLEpInfo's const char* fields are owned by the catalog
  // and may not survive past EnsureReady (which can re-scan / install).
  cap.name_from_info = info->name ? info->name : "";
  cap.ready_state_from_info = info->readyState;
  cap.certification_from_info = info->certification;
  cap.library_path_from_info = info->libraryPath ? info->libraryPath : "";
  out->push_back(std::move(cap));
  return TRUE;
}

}  // namespace

void StandaloneWinML_RegisterAllProviders(OrtEnv* env,
                                          const OrtApi* ort_api,
                                          PerformanceTestConfig& test_config) {
  if (env == nullptr || ort_api == nullptr) {
    ::fprintf(stderr, "[StandaloneWinML] RegisterAllProviders: env or ort_api is null\n");
    return;
  }

  if (g_catalog != nullptr) {
    ::fprintf(stderr, "[StandaloneWinML] RegisterAllProviders called twice — skipping (already registered)\n");
    return;
  }

  HRESULT hr = ::WinMLEpCatalogCreate(&g_catalog);
  if (FAILED(hr) || g_catalog == nullptr) {
    ::fprintf(stderr, "[StandaloneWinML] WinMLEpCatalogCreate failed: HRESULT=0x%08lX\n",
              static_cast<unsigned long>(hr));
    g_catalog = nullptr;
    return;
  }

  std::vector<EnumCaptured> captured;
  hr = ::WinMLEpCatalogEnumProviders(g_catalog, &EnumProvidersCallback, &captured);
  if (FAILED(hr)) {
    ::fprintf(stderr, "[StandaloneWinML] WinMLEpCatalogEnumProviders failed: HRESULT=0x%08lX\n",
              static_cast<unsigned long>(hr));
    // Release immediately so the failure state is clean ("no catalog, no
    // providers") rather than ambiguous ("catalog exists, no providers").
    ::WinMLEpCatalogRelease(g_catalog);
    g_catalog = nullptr;
    return;
  }

  ::fprintf(stdout, "[StandaloneWinML] WinML catalog enumerated %zu providers\n", captured.size());

  // Snapshot user-supplied --plugin_eps so we know whether to also drive
  // AppendPluginExecutionProviders' selection set. If the user didn't
  // explicitly select EPs we populate plugin_provider_type_list with the
  // ones we successfully register; otherwise we respect the user's filter.
  const bool user_supplied_plugin_eps =
      !test_config.machine_config.plugin_provider_type_list.empty();

  for (const auto& cap : captured) {
    ::fprintf(stdout, "[StandaloneWinML]   provider: %s  initial readyState=%s  certification=%s\n",
              cap.name_from_info.c_str(),
              ReadyStateString(cap.ready_state_from_info),
              CertificationString(cap.certification_from_info));

    if (cap.name_from_info.empty()) {
      ::fprintf(stderr, "[StandaloneWinML]     skip: empty name\n");
      continue;
    }

    if (IsBlacklisted(cap.name_from_info)) {
      ::fprintf(stdout, "[StandaloneWinML]     skip: blacklisted (pre-EnsureReady)\n");
      continue;
    }

    HRESULT ready_hr = ::WinMLEpEnsureReady(cap.handle);
    if (FAILED(ready_hr)) {
      ::fprintf(stderr,
                "[StandaloneWinML]     WinMLEpEnsureReady failed for '%s': HRESULT=0x%08lX — skipping registration\n",
                cap.name_from_info.c_str(), static_cast<unsigned long>(ready_hr));
      continue;
    }

    WinMLEpReadyState post_state = QueryEpReadyState(cap.handle);
    ::fprintf(stdout, "[StandaloneWinML]     post-EnsureReady readyState=%s\n",
              ReadyStateString(post_state));
    if (post_state != WinMLEpReadyState_Ready) {
      ::fprintf(stderr, "[StandaloneWinML]     skip: post-EnsureReady state is not Ready\n");
      continue;
    }

    // Re-query name + library path after EnsureReady — the catalog's view is
    // authoritative once a package may have been installed/refreshed.
    std::string name = QueryEpName(cap.handle);
    if (name.empty()) {
      name = cap.name_from_info;
    }
    if (IsBlacklisted(name)) {
      ::fprintf(stdout, "[StandaloneWinML]     skip: blacklisted (post-EnsureReady name='%s')\n",
                name.c_str());
      continue;
    }

    std::string lib_path_utf8 = QueryEpLibraryPath(cap.handle);
    if (lib_path_utf8.empty()) {
      lib_path_utf8 = cap.library_path_from_info;
    }
    if (lib_path_utf8.empty()) {
      ::fprintf(stderr, "[StandaloneWinML]     skip: empty libraryPath for '%s'\n", name.c_str());
      continue;
    }

    // ToPathString uses std::wstring_convert<std::codecvt_utf8<wchar_t>>,
    // which throws on malformed UTF-8. We continue with the next EP rather
    // than aborting registration.
    PathString lib_path_wide;
    try {
      lib_path_wide = ToPathString(lib_path_utf8);
    } catch (const std::exception& e) {
      ::fprintf(stderr,
                "[StandaloneWinML]     skip: libraryPath UTF-8 conversion failed for '%s': %s (raw='%s')\n",
                name.c_str(), e.what(), lib_path_utf8.c_str());
      continue;
    }

    ::fwprintf(stdout, L"[StandaloneWinML]     registering '%hs' from '%ls'\n",
               name.c_str(), lib_path_wide.c_str());

    OrtStatus* status = ort_api->RegisterExecutionProviderLibrary(
        env, name.c_str(), lib_path_wide.c_str());
    if (status != nullptr) {
      const char* msg = ort_api->GetErrorMessage(status);
      ::fprintf(stderr, "[StandaloneWinML]     RegisterExecutionProviderLibrary FAILED for '%s': %s\n",
                name.c_str(), msg ? msg : "(no message)");
      ort_api->ReleaseStatus(status);
      continue;
    }

    g_registered_eps.push_back(name);
    test_config.registered_plugin_eps.push_back(name);
    if (!user_supplied_plugin_eps) {
      test_config.machine_config.plugin_provider_type_list.push_back(name);
    }
    ::fprintf(stdout, "[StandaloneWinML]     registered OK\n");
  }

  ::fprintf(stdout, "[StandaloneWinML] RegisterAllProviders: %zu of %zu providers registered\n",
            g_registered_eps.size(), captured.size());
  if (user_supplied_plugin_eps) {
    ::fprintf(stdout,
              "[StandaloneWinML] (user supplied --plugin_eps; not auto-populating plugin_provider_type_list)\n");
  }
}

void StandaloneWinML_UnregisterAllProviders(OrtEnv* env,
                                            const OrtApi* ort_api,
                                            PerformanceTestConfig& test_config) noexcept {
  if (env != nullptr && ort_api != nullptr) {
    // Reverse order: dependent EPs unregister before their dependencies.
    for (auto it = g_registered_eps.rbegin(); it != g_registered_eps.rend(); ++it) {
      OrtStatus* status = ort_api->UnregisterExecutionProviderLibrary(env, it->c_str());
      if (status != nullptr) {
        const char* msg = ort_api->GetErrorMessage(status);
        ::fprintf(stderr, "[StandaloneWinML] UnregisterExecutionProviderLibrary failed for '%s': %s\n",
                  it->c_str(), msg ? msg : "(no message)");
        ort_api->ReleaseStatus(status);
      }
    }
  }

  // Strip our names out of test_config so the existing perftest cleanup
  // paths (main.cc:62 unregister_plugin_eps_at_scope_exit, which iterates
  // test_config.registered_plugin_eps and calls
  // OrtApi::UnregisterExecutionProviderLibrary) do not double-unregister.
  // Order matters: this must happen BEFORE the existing gsl::finally fires.
  // main.cc declares our gsl::finally AFTER the existing one, so LIFO runs
  // ours first and the strip below takes effect in time.
  EraseAll(test_config.registered_plugin_eps, g_registered_eps);
  EraseAll(test_config.machine_config.plugin_provider_type_list, g_registered_eps);

  g_registered_eps.clear();

  if (g_catalog != nullptr) {
    ::WinMLEpCatalogRelease(g_catalog);
    g_catalog = nullptr;
  }
}

}  // namespace perftest
}  // namespace onnxruntime

#endif  // BUILD_STANDALONE_WINML_PERF_TEST
