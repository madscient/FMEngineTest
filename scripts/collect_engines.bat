@echo off
rem collect_engines.bat
rem FmEngineApi 互換エンジン DLL を収集してビルド先ディレクトリにコピーする
rem
rem 使い方:
rem   scripts\collect_engines.bat [出力ディレクトリ]
rem   省略時は build\bin\Release を使用
rem
rem 前提:
rem   - Git が PATH に通っていること
rem   - CMake が PATH に通っていること
rem   - Visual Studio 2022 (Build Tools) がインストールされていること

setlocal enabledelayedexpansion

rem --- 出力ディレクトリ ---
set "OUT_DIR=%~1"
if "%OUT_DIR%"=="" set "OUT_DIR=build\bin\Release"

rem --- ビルド対象エンジンリスト (リポジトリ名のみ記載) ---
set ENGINES=YMEngine NukedEngine FMgenEngine DSAemuEngine DBOPLEngine SAASoundEngine

rem --- ワーク用ディレクトリ ---
set "ENGINES_DIR=engines"
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

    rem --- clone または pull ---
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

    rem --- submodule ---
    git -C "!REPO_DIR!" submodule update --init --recursive

    rem --- cmake configure ---
    set "BUILD_DIR=!REPO_DIR!\build"
    cmake -B "!BUILD_DIR!" -G "Visual Studio 17 2022" -A x64 -S "!REPO_DIR!"
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

    rem --- DLL コピー (Release ディレクトリを再帰検索) ---
    set "COPIED=0"
    for /r "!BUILD_DIR!" %%F in (%%E.dll) do (
        echo   Copying %%F -^> %OUT_DIR%\
        copy /y "%%F" "%OUT_DIR%\" >nul
        set "COPIED=1"
    )
    rem --- 名前が異なる場合も探す (lib*.dll など) ---
    if "!COPIED!"=="0" (
        for /r "!BUILD_DIR!\Release" %%F in (*.dll) do (
            echo   Copying %%F -^> %OUT_DIR%\
            copy /y "%%F" "%OUT_DIR%\" >nul
            set "COPIED=1"
        )
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
