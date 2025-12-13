Write-Host

$modelFileName = "PSD1.quant.onnx"
$modelFilePath = "x:\PSD\$modelFileName"

$cacheDir = (Split-Path $cacheFileName -Parent)
$cacheFileName = "x:\output\$modelFileName.cache.onnx"

$device = "openvino"
$powerMode = "performance"
$cacheExe = "generate_cache"

$selectDevice = "--select_ep_devices 2"

if ($device -eq "vitisai") {
    if ($cacheExe -eq "generate_cache") {
        $profileCommand = "$selectDevice -m times -t 0 -C ""ep.context_enable|1 ep.context_embed_mode|0 ep.context_file_path|$cacheFileName"" -I $modelFilePath"
    }
    elseif ($cacheExe -eq "use_cache") {
        $profileCommand = "$selectDevice -m duration -t $duration -I $cacheFileName"
    }
}
elseif ($device -eq "openvino") {
    $cacheDir = (Split-Path $cacheFileName -Parent)
    if ($powerMode -eq "powersaver") {
        if ($cacheExe -eq "generate_cache") {
            $profileCommand = "$selectDevice -o 0 -i `"device_type|NPU enable_qdq_optimizer|True cache_dir|$cacheDir`" -C `"ep.context_enable|1 ep.context_file_path|$cacheFileName`" -m times -t 0 -I $modelFilePath"
        }
        elseif ($cacheExe -eq "use_cache") {
            $profileCommand = "$selectDevice -o 0 -i `"device_type|NPU`" -m duration -t $duration -I $cacheFileName"
        }
    }
    elseif ($powerMode -eq "performance") {
        if ($cacheExe -eq "generate_cache") {
            $profileCommand = "$selectDevice -o 0 -i ""device_type|NPU enable_qdq_optimizer|True cache_dir|$cacheDir"" -C ""ep.context_enable|1 ep.context_file_path|$cacheFileName"" -m times -t 0 -I $modelFilePath"
        }
        elseif ($cacheExe -eq "use_cache") {
            $profileCommand = "$selectDevice -o 0 -i ""device_type|NPU"" -m duration -t $duration -I $cacheFileName"
        }
    }
}
elseif ($device -eq "qnn") {
    if ($powerMode -eq "powersaver") {
        if ($cacheExe -eq "generate_cache") {
            $profileCommand = "$selectDevice -i `"backend_path|QnnHtp.dll htp_performance_mode|extreme_power_saver soc_model|60 htp_graph_finalization_optimization_mode|3`" -C `"ep.context_enable|1 ep.context_file_path|$cacheFileName`" -m times -I $modelFilePath"
        }
        elseif ($cacheExe -eq "use_cache") {
            $profileCommand = "$selectDevice -i `"backend_path|QnnHtp.dll htp_performance_mode|extreme_power_saver soc_model|60 htp_graph_finalization_optimization_mode|3`" -C `"ep.context_enable|1`" -m duration -t $duration -I $cacheFileName"
        }
    }
    elseif ($powerMode -eq "performance") {
        if ($cacheExe -eq "generate_cache") {
            $profileCommand = "$selectDevice -i `"backend_path|QnnHtp.dll htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3`" -C `"ep.context_enable|1 ep.context_file_path|$cacheFileName`" -m times -I $modelFilePath"
        }
        elseif ($cacheExe -eq "use_cache") {
            $profileCommand = "$selectDevice -i `"backend_path|QnnHtp.dll htp_performance_mode|burst soc_model|60 htp_graph_finalization_optimization_mode|3`" -C `"ep.context_enable|1`" -m duration -t $duration -I $cacheFileName"
        }
    }
}

Write-Host $profileCommand
