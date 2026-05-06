// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include <core/session/onnxruntime_cxx_api.h>
#include <vector>
#include <string>

void WinML_InitializeAndRegisterAllProviders(Ort::Env& env, const std::vector<std::string>& provider_filter);
void WinML_Uninitialize(Ort::Env& env);
