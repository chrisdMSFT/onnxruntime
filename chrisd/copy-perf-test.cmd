@echo off
REM Script to copy winappsdk_onnxruntime_perf_test.exe and .pdb to OneDrive with date-commitish folder pattern
REM Usage: copy_perf_test.bat [BuildType] [Arch]
REM Example: copy_perf_test.bat RelWithDebInfo x64

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

REM Define the destination path
set DestBase=C:\Users\chrisd\OneDrive - Microsoft\winappsdk_onnxruntime_perf_test
set DestFolder=!DestBase!\!FolderName!\!Arch!

REM Verify source files exist
set ExePath=!SourceDir!\winappsdk_onnxruntime_perf_test.exe
set PdbPath=!SourceDir!\winappsdk_onnxruntime_perf_test.pdb

if not exist "!ExePath!" (
    echo Error: Executable not found: !ExePath!
    exit /b 1
)

if not exist "!PdbPath!" (
    echo Error: PDB file not found: !PdbPath!
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

copy "!PdbPath!" "!DestFolder!\" /Y
if errorlevel 1 (
    echo Error: Failed to copy PDB
    exit /b 1
)
echo Copied: !PdbPath! -^> !DestFolder!

REM Display summary
echo.
echo === Copy Summary ===
echo Date: !Date!
echo Commit: !Commit!
echo Folder: !FolderName!
echo Arch: !Arch!
echo Destination: !DestFolder!
echo Files copied:
echo   - winappsdk_onnxruntime_perf_test.exe
echo   - winappsdk_onnxruntime_perf_test.pdb
echo.
echo Success!

endlocal
