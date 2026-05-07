@echo off
REM Script to copy winml_standalone_perf_test.exe and required runtime DLLs
REM (Microsoft.Windows.AI.MachineLearning.dll, onnxruntime.dll, DirectML.dll)
REM to OneDrive with a date-commitish folder pattern.
REM Usage: copy-perf-test.cmd [BuildType] [Arch]
REM Example: copy-perf-test.cmd RelWithDebInfo x64

setlocal enabledelayedexpansion

set BuildType=%1
if "!BuildType!"=="" set BuildType=RelWithDebInfo

set Arch=%2
if "!Arch!"=="" set Arch=%Platform%
if "!Arch!"=="" set Arch=%VSCMD_ARG_TGT_ARCH%
if "!Arch!"=="" set Arch=%PROCESSOR_ARCHITECTURE%

if /I "!Arch!"=="AMD64" set Arch=x64
if /I "!Arch!"=="X86" set Arch=x86
if /I "!Arch!"=="ARM64" set Arch=arm64
if /I "!Arch!"=="ARM" set Arch=arm

set SourceDir=build\!BuildType!

REM Get the current date in YYMMDD format
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do (
    set DateString=%%c%%a%%b
)
set YY=!DateString:~2,2!
set MM=!DateString:~4,2!
set DD=!DateString:~6,2!
set Date=!YY!!MM!!DD!

REM Get the short commit hash
for /f "tokens=*" %%a in ('git rev-parse --short HEAD') do set Commit=%%a

if "!Commit!"=="" (
    echo Error: Failed to get git commit hash
    exit /b 1
)

REM Create the folder name with pattern: YYMMDD-commitish
set FolderName=!Date!-!Commit!

REM Define the destination path (kebab-case folder name)
set DestBase=C:\Users\chrisd\OneDrive - Microsoft\winml-standalone-perf-test
set DestFolder=!DestBase!\!FolderName!\!Arch!

REM Verify source files exist
set ExePath=!SourceDir!\winml_standalone_perf_test.exe
set MlDllPath=!SourceDir!\Microsoft.Windows.AI.MachineLearning.dll
set OrtDllPath=!SourceDir!\onnxruntime.dll
set DmlDllPath=!SourceDir!\DirectML.dll

if not exist "!ExePath!" (
    echo Error: Executable not found: !ExePath!
    exit /b 1
)

if not exist "!MlDllPath!" (
    echo Error: DLL not found: !MlDllPath!
    exit /b 1
)

if not exist "!OrtDllPath!" (
    echo Error: DLL not found: !OrtDllPath!
    exit /b 1
)

if not exist "!DmlDllPath!" (
    echo Error: DLL not found: !DmlDllPath!
    exit /b 1
)

REM Create destination directory if it doesn't exist
if not exist "!DestFolder!" (
    mkdir "!DestFolder!"
    echo Created directory: !DestFolder!
)

REM Copy files
copy "!ExePath!" "!DestFolder!\" /Y
if errorlevel 1 (
    echo Error: Failed to copy executable
    exit /b 1
)
echo Copied: !ExePath! -^> !DestFolder!

copy "!MlDllPath!" "!DestFolder!\" /Y
if errorlevel 1 (
    echo Error: Failed to copy Microsoft.Windows.AI.MachineLearning.dll
    exit /b 1
)
echo Copied: !MlDllPath! -^> !DestFolder!

copy "!OrtDllPath!" "!DestFolder!\" /Y
if errorlevel 1 (
    echo Error: Failed to copy onnxruntime.dll
    exit /b 1
)
echo Copied: !OrtDllPath! -^> !DestFolder!

copy "!DmlDllPath!" "!DestFolder!\" /Y
if errorlevel 1 (
    echo Error: Failed to copy DirectML.dll
    exit /b 1
)
echo Copied: !DmlDllPath! -^> !DestFolder!

REM Display summary
echo.
echo === Copy Summary ===
echo Date: !Date!
echo Commit: !Commit!
echo Folder: !FolderName!
echo Arch: !Arch!
echo Destination: !DestFolder!
echo Files copied:
echo   - winml_standalone_perf_test.exe
echo   - Microsoft.Windows.AI.MachineLearning.dll
echo   - onnxruntime.dll
echo   - DirectML.dll
echo.
echo Success!

endlocal
