// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include <core/session/onnxruntime_cxx_api.h>
#include <vector>
#include <string>

// RAII wrapper for the WinML EP catalog and any execution-provider libraries
// registered through it. Construction opens the catalog, enumerates providers,
// optionally filters by name, and registers each one with the supplied
// Ort::Env. Destruction unregisters the same set (best-effort) and releases
// the catalog. Lifetime invariant: the supplied Ort::Env must outlive this
// object.
//
// The catalog handle is held as a `void*` here so this header does not need
// to pull in `<WinMLEpCatalog.h>` (which transitively pulls in the WinML
// NuGet headers). The implementation file casts back to the real type.
class WinMLStandaloneRegistration {
 public:
  // Throws std::runtime_error on:
  //   * WinMLEpCatalogCreate failure
  //   * WinMLEpCatalogEnumProviders returning a failure HRESULT
  //   * provider_filter being non-empty and any requested provider failing
  //     to register
  // On throw, any partially-registered providers are unregistered before
  // the exception propagates.
  WinMLStandaloneRegistration(Ort::Env& env,
                              const std::vector<std::string>& provider_filter);
  ~WinMLStandaloneRegistration();

  WinMLStandaloneRegistration(const WinMLStandaloneRegistration&) = delete;
  WinMLStandaloneRegistration& operator=(const WinMLStandaloneRegistration&) = delete;
  WinMLStandaloneRegistration(WinMLStandaloneRegistration&&) = delete;
  WinMLStandaloneRegistration& operator=(WinMLStandaloneRegistration&&) = delete;

  const std::vector<std::string>& registered_providers() const noexcept { return registered_; }

 private:
  void Cleanup() noexcept;

  Ort::Env& env_;
  void* catalog_ = nullptr;  // WinMLEpCatalogHandle (opaque to keep WinML headers out of this header)
  std::vector<std::string> registered_;
};
