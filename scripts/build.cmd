@echo off
REM Load MSVC x64 env, then build the Iris target (Release by default).
REM Output is left to the caller's redirection.
REM NOTE: ASCII-only (cmd.exe reads OEM/GBK on zh-CN Windows).
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] failed to load vcvars64.bat 1>&2
    exit /b 1
)
cmake --build build --config Release %*
