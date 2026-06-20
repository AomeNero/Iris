@echo off
title Iris - manual build
REM Manual compile: load MSVC env, configure + build Release, then wait for keypress.
REM Double-click friendly: switches to project root first; window stays open at end.
REM Usage: double-click this file, or run from anywhere.
REM NOTE: ASCII-only (cmd.exe reads OEM/GBK on zh-CN Windows).

pushd "%~dp0.."

call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build-manual] failed to load vcvars64.bat
    goto end
)

echo [build-manual] configuring...
cmake -B build -Wno-dev
if errorlevel 1 (
    echo [build-manual] configure FAILED.
    goto end
)

echo [build-manual] building Release...
cmake --build build --config Release
if errorlevel 1 (
    echo [build-manual] build FAILED.
) else (
    echo [build-manual] build OK: build\Release\Iris.exe
)

:end
popd
echo.
pause
