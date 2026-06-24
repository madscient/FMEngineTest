@echo off
rem collect_engines.bat
rem Collect FmEngineApi-compatible engine DLLs by cloning and building them.
rem
rem Usage:
rem   scripts\collect_engines.bat [output_directory]
rem   Default output: build\bin\Release
rem
rem Requirements:
rem   - Git in PATH
rem   - CMake in PATH
rem   - Visual Studio 2022 (or Build Tools) installed

setlocal enabledelayedexpansion

rem --- Output directory ---
set "OUT_DIR=%~1"
if "%OUT_DIR%"=="" set "OUT_DIR=build\bin\Release"

rem --- Engine list (repository names only) ---
set ENGINES=YMEngine NukedEngine FMgenEngine DSAemuEngine DBOPLEngine SAASoundEngine SCCIBridgeEngine

rem --- Working directory ---
set "ENGINES_DIR=..\."
set "GITHUB_BASE=https://github.com/madscient"

echo ============================================================
echo  FMEngineTest: collect_engines.bat
echo  Output dir : %OUT_DIR%
echo  Engines dir: %ENGINES_DIR%
echo ============================================================
echo.

if not exist "%OUT_DIR%" (
    echo Creating output directory: %OUT_DIR%
    mkdir "%OUT_DIR%"
)
if not exist "%ENGINES_DIR%" mkdir "%ENGINES_DIR%"

set FAILED=
set SUCCESS=

for %%E in (%ENGINES%) do (
    echo ------------------------------------------------------------
    echo  Engine: %%E
    echo ------------------------------------------------------------

    set "REPO_DIR=%ENGINES_DIR%\%%E"

    rem --- clone or pull ---
    if exist "!REPO_DIR!\.git" (
        echo   Updating !REPO_DIR! ...
        git -C "!REPO_DIR!" pull --ff-only
    ) else (
        echo   Cloning %GITHUB_BASE%/%%E ...
        git clone --depth 1 "%GITHUB_BASE%/%%E" "!REPO_DIR!"
    )
    if errorlevel 1 (
        echo   [ERROR] git failed for %%E
        set "FAILED=!FAILED! %%E"
        goto :next_%%E
    )

    rem --- submodules ---
    git -C "!REPO_DIR!" submodule update --init --recursive

    rem --- cmake configure ---
    set "BUILD_DIR=!REPO_DIR!\build"
    cmake -B "!BUILD_DIR!" -G "Visual Studio 16 2019" -A x64 -S "!REPO_DIR!"
    if errorlevel 1 (
        echo   [ERROR] cmake configure failed for %%E
        set "FAILED=!FAILED! %%E"
        goto :next_%%E
    )

    rem --- cmake build ---
    cmake --build "!BUILD_DIR!" --config Release
    if errorlevel 1 (
        echo   [ERROR] cmake build failed for %%E
        set "FAILED=!FAILED! %%E"
        goto :next_%%E
    )

    rem --- Copy all DLLs found anywhere under BUILD_DIR ---
    rem     "for /r" fails when the path exceeds MAX_PATH (260 chars).
    rem     Use "dir /s /b" instead, which handles long paths correctly.
    set "COPIED=0"
    for /f "usebackq delims=" %%F in (`dir /s /b "!BUILD_DIR!\*.dll" 2^>nul`) do (
        echo   Copying %%F -> %OUT_DIR%\
        copy /y "%%F" "%OUT_DIR%\" >nul
        set "COPIED=1"
    )
    if "!COPIED!"=="0" (
        echo   [WARN] No DLL found in build output for %%E
    ) else (
        set "SUCCESS=!SUCCESS! %%E"
    )

    :next_%%E
    echo.
)

echo ============================================================
echo  Results
echo ============================================================
if not "%SUCCESS%"=="" echo  Success:%SUCCESS%
if not "%FAILED%"==""  echo  Failed :%FAILED%
echo ============================================================

endlocal
