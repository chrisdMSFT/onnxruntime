# PowerShell script to extract include directories and find all header files for a CMake target

param(
    [string]$InputFile = "target-onnxruntime_perf_test-RelWithDebInfo.json",
    [string]$OutputFile = "target_includes.txt",
    [switch]$FindAllHeaders,
    [switch]$OnlyValidPaths
)

# Get the script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$InputPath = Join-Path $ScriptDir $InputFile
$OutputPath = Join-Path $ScriptDir $OutputFile

if (-not (Test-Path $InputPath)) {
    Write-Error "Input file not found: $InputPath"
    exit 1
}

try {
    Write-Host "Reading JSON file: $InputPath"
    $jsonContent = Get-Content $InputPath -Raw | ConvertFrom-Json

    # Extract include directories from compile command fragments
    $includeDirectories = @()
    $allFragments = @()

    if ($jsonContent.compileGroups) {
        foreach ($group in $jsonContent.compileGroups) {
            if ($group.compileCommandFragments) {
                foreach ($fragmentObj in $group.compileCommandFragments) {
                    if ($fragmentObj.fragment) {
                        $fragment = $fragmentObj.fragment
                        $allFragments += $fragment

                        # Extract include paths from different patterns
                        # Pattern 1: /external:I<path>
                        if ($fragment -match '^/external:I(.+)$') {
                            $includeDirectories += $matches[1]
                        }
                        # Pattern 2: /I<path> (standard MSVC include)
                        elseif ($fragment -match '^/I(.+)$') {
                            $includeDirectories += $matches[1]
                        }
                        # Pattern 3: -I<path> (GCC style)
                        elseif ($fragment -match '^-I(.+)$') {
                            $includeDirectories += $matches[1]
                        }
                    }
                }
            }
        }
    }

    # Remove duplicates and sort
    $uniqueIncludeDirs = $includeDirectories | Sort-Object -Unique

    # Filter to only existing directories if requested
    if ($OnlyValidPaths) {
        $validIncludeDirs = @()
        foreach ($dir in $uniqueIncludeDirs) {
            # Handle both forward and back slashes, normalize path
            $normalizedPath = $dir -replace '/', '\'
            if (Test-Path $normalizedPath -PathType Container) {
                $validIncludeDirs += $dir
            } else {
                Write-Warning "Include directory not found: $normalizedPath"
            }
        }
        $uniqueIncludeDirs = $validIncludeDirs
    }

    Write-Host "Found $($uniqueIncludeDirs.Count) unique include directories"

    # Save include directories
    $uniqueIncludeDirs | Out-File -FilePath $OutputPath -Encoding UTF8
    Write-Host "Include directories saved to: $OutputPath"

    # Display include directories
    Write-Host "`nInclude Directories:"
    Write-Host "==================="
    foreach ($dir in $uniqueIncludeDirs) {
        Write-Host $dir
    }

    # If requested, find all header files in these directories
    if ($FindAllHeaders) {
        Write-Host "`nScanning for header files..."
        $headerExtensions = @('*.h', '*.hpp', '*.hxx', '*.h++', '*.hh')
        $allHeaders = @()

        foreach ($includeDir in $uniqueIncludeDirs) {
            $normalizedPath = $includeDir -replace '/', '\'
            if (Test-Path $normalizedPath -PathType Container) {
                Write-Host "Scanning: $normalizedPath"
                foreach ($ext in $headerExtensions) {
                    try {
                        $headers = Get-ChildItem -Path $normalizedPath -Filter $ext -Recurse -File -ErrorAction SilentlyContinue
                        $allHeaders += $headers.FullName
                    } catch {
                        Write-Warning "Error scanning $normalizedPath for $ext : $($_.Exception.Message)"
                    }
                }
            }
        }

        $uniqueHeaders = $allHeaders | Sort-Object -Unique
        $headersOutputPath = $OutputPath.Replace(".txt", "_headers.txt")
        $uniqueHeaders | Out-File -FilePath $headersOutputPath -Encoding UTF8

        Write-Host "`nFound $($uniqueHeaders.Count) header files"
        Write-Host "Header files saved to: $headersOutputPath"

        # Group by directory for summary
        $headersByDir = $uniqueHeaders | Group-Object { Split-Path $_ -Parent } | Sort-Object Count -Descending
        Write-Host "`nHeader files by directory (top 10):"
        Write-Host "==================================="
        $headersByDir | Select-Object -First 10 | ForEach-Object {
            Write-Host "$($_.Count) files in $($_.Name)"
        }
    }

    # Create summary report
    $summaryPath = $OutputPath.Replace(".txt", "_summary.txt")
    $summary = @"
CMake Target Include Analysis Summary
====================================
Target JSON: $InputFile
Analysis Date: $(Get-Date)

Include Directories Found: $($uniqueIncludeDirs.Count)
$(if ($FindAllHeaders) { "Header Files Found: $($uniqueHeaders.Count)" } else { "Header files not scanned (use -FindAllHeaders to scan)" })

Include Directories:
$(($uniqueIncludeDirs | ForEach-Object { "  $_" }) -join "`n")

Compile Command Fragments Analyzed: $($allFragments.Count)
"@

    $summary | Out-File -FilePath $summaryPath -Encoding UTF8
    Write-Host "`nSummary saved to: $summaryPath"

} catch {
    Write-Error "Error processing JSON file: $($_.Exception.Message)"
    exit 1
}
