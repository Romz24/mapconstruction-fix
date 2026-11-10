@echo off
rem Builds build\test_markers.exe and runs it against the game's IMG archives.
rem Usage: test.bat "E:\Games\GTA San Andreas"      (game folder; GTAINTERFACE.dll is taken from its editor\ subfolder)
setlocal
cd /d "%~dp0"
if "%~1"=="" (
    echo usage: test.bat "^<GTA San Andreas folder^>"
    exit /b 2
)
set "GAME=%~1"
set "EDITOR=%GAME%\editor"
if not exist "%EDITOR%\GTAINTERFACE.dll" set "EDITOR=%GAME%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

if not exist build mkdir build
cl /nologo /O2 /MT /W3 test\test_markers.c /Fo"build\\" /Fe"build\test_markers.exe" /link /NOLOGO kernel32.lib user32.lib shell32.lib || exit /b 1

build\test_markers.exe "%EDITOR%" "%GAME%\SAMP\SAMP.img" "%GAME%\SAMP\custom.img" "%GAME%\models\gta3.img" "%GAME%\models\gta_int.img"
