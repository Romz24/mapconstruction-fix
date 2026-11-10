@echo off
rem Builds build\version.dll (32-bit) - proxy + in-memory patch for SA-MP Map Construction editor.exe.
rem Usage:
rem   build.bat                                        - build only
rem   build.bat "E:\Games\GTA San Andreas\editor"      - build and copy version.dll next to editor.exe
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [error] vswhere.exe not found - install Visual Studio 2019+ with "Desktop development with C++".
    exit /b 1
)
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [error] Visual Studio with C++ x86/x64 tools not found.
    exit /b 1
)

rem vcvarsall needs vswhere on PATH, otherwise it prints a harmless warning
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || (
    echo [error] vcvarsall.bat x86 failed.
    exit /b 1
)

if not exist build mkdir build
cl /nologo /O2 /GS- /MT /W4 /LD src\version_proxy.c /Fo"build\\" /Fe"build\version.dll" ^
   /link /DEF:src\version.def /NOLOGO /DYNAMICBASE:NO /INCREMENTAL:NO kernel32.lib user32.lib
if errorlevel 1 (
    echo [error] build failed.
    exit /b 1
)
echo [ok] build\version.dll

if "%~1"=="" exit /b 0
if not exist "%~1\editor.exe" (
    echo [error] "%~1\editor.exe" not found - pass the folder that contains editor.exe.
    exit /b 1
)
copy /y "build\version.dll" "%~1\version.dll" >nul || (
    echo [error] copy failed - is the editor running?
    exit /b 1
)
echo [ok] installed to "%~1\version.dll"
exit /b 0
