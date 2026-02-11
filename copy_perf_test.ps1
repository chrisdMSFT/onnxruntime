# Script to copy winappsdk_onnxruntime_perf_test.exe and .pdb to OneDrive with date-commitish folder pattern
# Usage: .\copy_perf_test.ps1 -BuildType Release -SourceDir ".\build\Release" -ExeDir "bin"

param(
    [string]$BuildType = "Release",
    [string]$SourceDir = ".\build\Release",
    [string]$ExeDir = "bin"
)

# Get the current date in YYMMDD format
$date = Get-Date -Format "yyMMdd"

# Get the short commit hash
try {
    $commit = & git rev-parse --short HEAD
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to get git commit hash"
        exit 1
    }
} catch {
    Write-Error "Failed to get git commit hash: $_"
    exit 1
}

# Create the folder name with pattern: YYMMDD-commitish
$folderName = "$date-$commit"

# Define the destination path
$destBase = "C:\Users\chrisd\OneDrive - Microsoft\winappsdk_onnxruntime_perf_test"
$destFolder = Join-Path $destBase $folderName

# Verify source files exist
$exePath = Join-Path $SourceDir "$ExeDir\winappsdk_onnxruntime_perf_test.exe"
$pdbPath = Join-Path $SourceDir "$ExeDir\winappsdk_onnxruntime_perf_test.pdb"

if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found: $exePath"
    exit 1
}

if (-not (Test-Path $pdbPath)) {
    Write-Error "PDB file not found: $pdbPath"
    exit 1
}

# Create destination directory if it doesn't exist
if (-not (Test-Path $destFolder)) {
    New-Item -ItemType Directory -Path $destFolder -Force | Out-Null
    Write-Host "Created directory: $destFolder"
}

# Copy files
try {
    Copy-Item -Path $exePath -Destination $destFolder -Force
    Write-Host "Copied: $exePath -> $destFolder"

    Copy-Item -Path $pdbPath -Destination $destFolder -Force
    Write-Host "Copied: $pdbPath -> $destFolder"

    Write-Host "Successfully copied files to: $destFolder"
} catch {
    Write-Error "Failed to copy files: $_"
    exit 1
}

# Display summary
Write-Host ""
Write-Host "=== Copy Summary ===" -ForegroundColor Green
Write-Host "Date: $date"
Write-Host "Commit: $commit"
Write-Host "Folder: $folderName"
Write-Host "Destination: $destFolder"
Write-Host "Files copied:"
Write-Host "  - winappsdk_onnxruntime_perf_test.exe"
Write-Host "  - winappsdk_onnxruntime_perf_test.pdb"
