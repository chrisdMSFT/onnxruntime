# Complete transitive closure analysis for header dependencies

param(
    [string]$InputFile = "target-onnxruntime_perf_test-RelWithDebInfo.json",
    [string]$OutputFile = "header_transitive_closure.txt",
    [switch]$UseCompiler,
    [switch]$Detailed,
    [string]$CompilerPath = "cl.exe"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$InputPath = Join-Path $ScriptDir $InputFile

if (-not (Test-Path $InputPath)) {
    Write-Error "Input file not found: $InputPath"
    exit 1
}

# Parse the target JSON to get sources and includes
function Get-TargetInfo {
    param([string]$JsonPath)

    $jsonContent = Get-Content $JsonPath -Raw | ConvertFrom-Json

    # Extract source files
    $sourceFiles = @()
    if ($jsonContent.sources) {
        foreach ($source in $jsonContent.sources) {
            if ($source.path -and $source.path -match '\.(cc|cpp|c|cxx)$') {
                $sourceFiles += $source.path
            }
        }
    }

    # Extract include directories
    $includeDirectories = @()
    if ($jsonContent.compileGroups) {
        foreach ($group in $jsonContent.compileGroups) {
            if ($group.compileCommandFragments) {
                foreach ($fragmentObj in $group.compileCommandFragments) {
                    $fragment = $fragmentObj.fragment

                    # Extract include paths
                    if ($fragment -match '^/external:I(.+)$') {
                        $includeDirectories += $matches[1]
                    } elseif ($fragment -match '^/I(.+)$') {
                        $includeDirectories += $matches[1]
                    } elseif ($fragment -match '^-I(.+)$') {
                        $includeDirectories += $matches[1]
                    }
                }
            }
        }
    }

    # Extract compile definitions
    $defines = @()
    if ($jsonContent.compileGroups) {
        foreach ($group in $jsonContent.compileGroups) {
            if ($group.defines) {
                foreach ($defineObj in $group.defines) {
                    if ($defineObj.define) {
                        $defines += "/D$($defineObj.define)"
                    }
                }
            }
        }
    }

    return @{
        SourceFiles = $sourceFiles | Sort-Object -Unique
        IncludeDirectories = $includeDirectories | Sort-Object -Unique
        Defines = $defines | Sort-Object -Unique
    }
}

# Use compiler to get complete dependency tree
function Get-CompilerDependencies {
    param(
        [string[]]$SourceFiles,
        [string[]]$IncludePaths,
        [string[]]$Defines,
        [string]$Compiler
    )

    $allHeaders = @{}
    $dependencyTree = @{}

    foreach ($sourceFile in $SourceFiles) {
        Write-Host "Analyzing dependencies for: $sourceFile"

        # Normalize path for Windows
        $normalizedSource = $sourceFile -replace '/', '\'

        if (-not (Test-Path $normalizedSource)) {
            Write-Warning "Source file not found: $normalizedSource"
            continue
        }

        # Build compiler arguments
        $includeArgs = $IncludePaths | ForEach-Object {
            $normalizedPath = $_ -replace '/', '\'
            "/I`"$normalizedPath`""
        }
        $defineArgs = $Defines

        # Use /showIncludes to get dependency tree
        $args = @('/showIncludes', '/EP', '/C') + $defineArgs + $includeArgs + @("`"$normalizedSource`"")

        try {
            $startInfo = New-Object System.Diagnostics.ProcessStartInfo
            $startInfo.FileName = $Compiler
            $startInfo.Arguments = $args -join ' '
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true

            $process = New-Object System.Diagnostics.Process
            $process.StartInfo = $startInfo
            $process.Start() | Out-Null

            $output = $process.StandardOutput.ReadToEnd()
            $errorOutput = $process.StandardError.ReadToEnd()
            $process.WaitForExit()

            if ($process.ExitCode -eq 0 -or $output) {
                # Parse include tree
                $lines = $output -split "`n"
                $currentDepth = 0
                $stack = @()

                foreach ($line in $lines) {
                    if ($line -match '^Note: including file:\s*(.+)$') {
                        $headerPath = $matches[1].Trim()
                        $depth = ($line | Select-String -Pattern '\s+' -AllMatches).Matches.Count - 3

                        # Track all headers
                        $allHeaders[$headerPath] = $true

                        # Build dependency tree
                        if ($depth -eq 0) {
                            $stack = @($headerPath)
                        } else {
                            # Adjust stack to current depth
                            if ($depth -lt $stack.Count) {
                                $stack = $stack[0..($depth-1)] + $headerPath
                            } else {
                                $stack += $headerPath
                            }
                        }

                        # Record dependency relationship
                        if ($stack.Count -gt 1) {
                            $parent = $stack[$stack.Count - 2]
                            if (-not $dependencyTree.ContainsKey($parent)) {
                                $dependencyTree[$parent] = @()
                            }
                            if ($dependencyTree[$parent] -notcontains $headerPath) {
                                $dependencyTree[$parent] += $headerPath
                            }
                        }
                    }
                }
            } else {
                Write-Warning "Compiler error for $sourceFile : $errorOutput"
            }
        } catch {
            Write-Warning "Failed to analyze $sourceFile : $($_.Exception.Message)"
        }
    }

    return @{
        AllHeaders = $allHeaders.Keys | Sort-Object
        DependencyTree = $dependencyTree
    }
}

# Manual header parsing (fallback method)
function Get-ManualDependencies {
    param(
        [string[]]$SourceFiles,
        [string[]]$IncludePaths
    )

    $allHeaders = @{}
    $processed = @{}

    function Find-HeaderFile {
        param([string]$HeaderName, [string[]]$SearchPaths, [string]$CurrentDir)

        # Try relative to current file first
        $relativePath = Join-Path $CurrentDir $HeaderName
        if (Test-Path $relativePath) {
            return $relativePath
        }

        # Try each include path
        foreach ($includePath in $SearchPaths) {
            $normalizedPath = $includePath -replace '/', '\'
            $fullPath = Join-Path $normalizedPath $HeaderName
            if (Test-Path $fullPath) {
                return $fullPath
            }
        }

        return $null
    }

    function Process-File {
        param([string]$FilePath, [string[]]$SearchPaths, [int]$Depth = 0)

        if ($Depth -gt 50) { return } # Prevent infinite recursion

        $normalizedPath = $FilePath -replace '/', '\'
        if ($processed.ContainsKey($normalizedPath)) { return }
        $processed[$normalizedPath] = $true

        if (-not (Test-Path $normalizedPath)) { return }

        $currentDir = Split-Path $normalizedPath -Parent

        try {
            $content = Get-Content $normalizedPath -ErrorAction Stop
            foreach ($line in $content) {
                if ($line -match '^\s*#include\s*[<"]([^>"]+)[>"]') {
                    $headerName = $matches[1]
                    $headerPath = Find-HeaderFile $headerName $SearchPaths $currentDir

                    if ($headerPath) {
                        $allHeaders[$headerPath] = $true
                        Process-File $headerPath $SearchPaths ($Depth + 1)
                    }
                }
            }
        } catch {
            Write-Warning "Could not read file: $normalizedPath"
        }
    }

    foreach ($sourceFile in $SourceFiles) {
        Write-Host "Manually parsing: $sourceFile"
        Process-File $sourceFile $IncludePaths
    }

    return $allHeaders.Keys | Sort-Object
}

# Main execution
try {
    Write-Host "Analyzing target JSON: $InputPath"
    $targetInfo = Get-TargetInfo $InputPath

    Write-Host "`nTarget Analysis:"
    Write-Host "==============="
    Write-Host "Source files: $($targetInfo.SourceFiles.Count)"
    Write-Host "Include directories: $($targetInfo.IncludeDirectories.Count)"
    Write-Host "Compile definitions: $($targetInfo.Defines.Count)"

    if ($Detailed) {
        Write-Host "`nSource Files:"
        $targetInfo.SourceFiles | ForEach-Object { Write-Host "  $_" }

        Write-Host "`nInclude Directories:"
        $targetInfo.IncludeDirectories | ForEach-Object { Write-Host "  $_" }
    }

    $UseCompiler = $true

    # Perform dependency analysis
    if ($UseCompiler) {
        Write-Host "`nUsing compiler for dependency analysis..."
        $result = Get-CompilerDependencies -SourceFiles $targetInfo.SourceFiles -IncludePaths $targetInfo.IncludeDirectories -Defines $targetInfo.Defines -Compiler $CompilerPath
        $allHeaders = $result.AllHeaders
        $dependencyTree = $result.DependencyTree
    } else {
        Write-Host "`nUsing manual parsing for dependency analysis..."
        $allHeaders = Get-ManualDependencies -SourceFiles $targetInfo.SourceFiles -IncludePaths $targetInfo.IncludeDirectories
        $dependencyTree = @{}
    }

    Write-Host "`nTransitive Closure Analysis Complete"
    Write-Host "===================================="
    Write-Host "Total unique headers: $($allHeaders.Count)"

    # Save results
    $outputPath = Join-Path $ScriptDir $OutputFile
    $allHeaders | Out-File -FilePath $outputPath -Encoding UTF8
    Write-Host "Header list saved to: $outputPath"

    # Group headers by directory
    $headersByDir = $allHeaders | Group-Object { Split-Path $_ -Parent } | Sort-Object Count -Descending

    Write-Host "`nHeaders by Directory (Top 20):"
    Write-Host "==============================="
    $headersByDir | Select-Object -First 20 | ForEach-Object {
        Write-Host "$($_.Count) headers in $($_.Name)"
    }

    # Save detailed report
    if ($UseCompiler -and $dependencyTree.Count -gt 0) {
        $treeOutputPath = $outputPath.Replace(".txt", "_tree.txt")
        $treeReport = @"
Header Dependency Tree
=====================
Generated: $(Get-Date)

"@
        foreach ($parent in $dependencyTree.Keys | Sort-Object) {
            $treeReport += "`n$parent includes:`n"
            foreach ($child in $dependencyTree[$parent] | Sort-Object) {
                $treeReport += "  -> $child`n"
            }
        }

        $treeReport | Out-File -FilePath $treeOutputPath -Encoding UTF8
        Write-Host "Dependency tree saved to: $treeOutputPath"
    }

    # File type analysis
    $extensions = $allHeaders | ForEach-Object { [System.IO.Path]::GetExtension($_).ToLower() } |
                  Where-Object { $_ } | Group-Object | Sort-Object Count -Descending

    Write-Host "`nFile Type Distribution:"
    Write-Host "======================="
    foreach ($ext in $extensions) {
        Write-Host "$($ext.Name): $($ext.Count) files"
    }

} catch {
    Write-Error "Error during analysis: $($_.Exception.Message)"
    exit 1
}
