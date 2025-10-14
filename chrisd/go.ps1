# PowerShell script to extract compileCommandFragments from target-onnxruntime_perf_test-RelWithDebInfo.json

param(
    [string]$InputFile = "target-onnxruntime_perf_test-RelWithDebInfo.json",
    [string]$OutputFile = "compile_command_fragments.txt"
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

    # Initialize array to collect all fragments
    $allFragments = @()

    # Extract compileCommandFragments from each compile group
    if ($jsonContent.compileGroups) {
        foreach ($group in $jsonContent.compileGroups) {
            if ($group.compileCommandFragments) {
                foreach ($fragmentObj in $group.compileCommandFragments) {
                    if ($fragmentObj.fragment) {
                        $allFragments += $fragmentObj.fragment
                    }
                }
            }
        }
    }

    # Output results
    Write-Host "Found $($allFragments.Count) compile command fragments"

    # Save to output file
    $allFragments | Out-File -FilePath $OutputPath -Encoding UTF8
    Write-Host "Fragments saved to: $OutputPath"

    # Also display on console
    Write-Host "`nCompile Command Fragments:"
    Write-Host "========================="
    foreach ($fragment in $allFragments) {
        Write-Host $fragment
    }

    # Create a summary
    Write-Host "`nSummary:"
    Write-Host "--------"
    Write-Host "Total fragments: $($allFragments.Count)"
    Write-Host "Output file: $OutputPath"

} catch {
    Write-Error "Error processing JSON file: $($_.Exception.Message)"
    exit 1
}
