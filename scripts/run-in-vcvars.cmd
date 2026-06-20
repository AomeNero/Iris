@echo off
REM Load MSVC (VS18/2026) x64 dev environment, then run the given command.
REM Usage: scripts\run-in-vcvars.cmd ^<command^> [args...]
REM Example: scripts\run-in-vcvars.cmd cmake -B build
REM NOTE: Keep this file ASCII-only (cmd.exe reads OEM/GBK on zh-CN Windows).
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [run-in-vcvars] failed to load vcvars64.bat 1>&2
    exit /b 1
)
%*
