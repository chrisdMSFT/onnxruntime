// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <Unknwn.h>
#include <winrt/base.h> // for winrt::hresult_error
#include <vector>
#include <string>

void WinAppSDK_WinMLInitializeMLAndRegisterAllProviders(const char* const winappsdk_version, const std::vector<std::string>& provider_list);
void WinAppSDK_WinMLUninitialize();
