# PowerShell script to extract sources from target-onnxruntime_perf_test-RelWithDebInfo.json

param(
    [string]$InputFile = "target-onnxruntime_perf_test-RelWithDebInfo.json",
    [string]$OutputFile = "extracted_sources.txt",
    [switch]$SeparateByType,
    [switch]$IncludeMetadata
)

# Get the script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Construct full paths
$InputPath = Join-Path $ScriptDir $InputFile
$OutputPath = Join-Path $ScriptDir $OutputFile

# Check if input file exists
if (-not (Test-Path $InputPath)) {
    Write-Error "Input file not found: $InputPath"
    exit 1
}

try {
    Write-Host "Reading JSON file: $InputPath"

    # Read and parse the JSON file
    $jsonContent = Get-Content $InputPath -Raw | ConvertFrom-Json

    # Initialize arrays to collect sources
    $allSources = @()
    $sourceFiles = @()
    $headerFiles = @()
    $otherFiles = @()

    # Extract sources
    if ($jsonContent.sources) {
        foreach ($source in $jsonContent.sources) {
            if ($source.path) {
                $sourcePath = $source.path

                # Create source object with metadata if requested
                if ($IncludeMetadata) {
                    $sourceObj = [PSCustomObject]@{
                        Path = $sourcePath
                        Backtrace = $source.backtrace
                        SourceGroupIndex = $source.sourceGroupIndex
                        CompileGroupIndex = $source.compileGroupIndex
                        FileExtension = [System.IO.Path]::GetExtension($sourcePath)
                    }
                    $allSources += $sourceObj
                } else {
                    $allSources += $sourcePath
                }

                # Categorize by file extension if requested
                if ($SeparateByType) {
                    $extension = [System.IO.Path]::GetExtension($sourcePath).ToLower()
                    switch ($extension) {
                        {$_ -in '.cc', '.cpp', '.c', '.cxx'} { $sourceFiles += $sourcePath }
                        {$_ -in '.h', '.hpp', '.hxx'} { $headerFiles += $sourcePath }
                        default { $otherFiles += $sourcePath }
                    }
                }
            }
        }
    }

    # Output results
    Write-Host "Found $($allSources.Count) source files"

    if ($SeparateByType) {
        # Save categorized files
        $sourceFilesPath = $OutputPath.Replace(".txt", "_sources.txt")
        $headerFilesPath = $OutputPath.Replace(".txt", "_headers.txt")
        $otherFilesPath = $OutputPath.Replace(".txt", "_other.txt")

        $sourceFiles | Out-File -FilePath $sourceFilesPath -Encoding UTF8
        $headerFiles | Out-File -FilePath $headerFilesPath -Encoding UTF8
        $otherFiles | Out-File -FilePath $otherFilesPath -Encoding UTF8

        Write-Host "Source files (.c, .cc, .cpp): $($sourceFiles.Count) -> $sourceFilesPath"
        Write-Host "Header files (.h, .hpp): $($headerFiles.Count) -> $headerFilesPath"
        Write-Host "Other files: $($otherFiles.Count) -> $otherFilesPath"
    } else {
        # Save all files to single output
        if ($IncludeMetadata) {
            # Save as CSV for better readability with metadata
            $csvPath = $OutputPath.Replace(".txt", ".csv")
            $allSources | Export-Csv -Path $csvPath -NoTypeInformation
            Write-Host "Sources with metadata saved to: $csvPath"
        } else {
            $allSources | Out-File -FilePath $OutputPath -Encoding UTF8
            Write-Host "All sources saved to: $OutputPath"
        }
    }

    # Display summary on console
    Write-Host "`nSource Files Summary:"
    Write-Host "===================="

    if ($SeparateByType) {
        Write-Host "Source files (.c, .cc, .cpp, .cxx): $($sourceFiles.Count)"
        Write-Host "Header files (.h, .hpp, .hxx): $($headerFiles.Count)"
        Write-Host "Other files: $($otherFiles.Count)"
        Write-Host "Total: $($allSources.Count)"

        Write-Host "`nSource Files:"
        $sourceFiles | ForEach-Object { Write-Host "  $_" }

        Write-Host "`nHeader Files:"
        $headerFiles | ForEach-Object { Write-Host "  $_" }

        if ($otherFiles.Count -gt 0) {
            Write-Host "`nOther Files:"
            $otherFiles | ForEach-Object { Write-Host "  $_" }
        }
    } else {
        Write-Host "Total files: $($allSources.Count)"
        Write-Host "`nAll Sources:"
        if ($IncludeMetadata) {
            $allSources | ForEach-Object {
                Write-Host "  $($_.Path) (Group: $($_.SourceGroupIndex), Compile: $($_.CompileGroupIndex))"
            }
        } else {
            $allSources | ForEach-Object { Write-Host "  $_" }
        }
    }

    # File type statistics
    $extensions = $allSources | ForEach-Object {
        if ($IncludeMetadata) { $_.FileExtension }
        else { [System.IO.Path]::GetExtension($_) }
    } | Group-Object | Sort-Object Count -Descending

    Write-Host "`nFile Type Statistics:"
    Write-Host "--------------------"
    foreach ($ext in $extensions) {
        $extName = if ($ext.Name) { $ext.Name } else { "(no extension)" }
        Write-Host "$extName : $($ext.Count) files"
    }

} catch {
    Write-Error "Error processing JSON file: $($_.Exception.Message)"
    exit 1
}
