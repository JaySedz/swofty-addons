@echo off
rem Builds native\cockpit_hud.dll. Needs Visual Studio 2022 or later with "Desktop development with C++".
rem The UI files in ui\ are built separately by tools\build_ui.py (Python 3), from the game's own files.
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
set FLAGS=/nologo /utf-8 /std:c++latest /Zc:preprocessor /permissive- /EHsc /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN
cl %FLAGS% /LD /Inative\include native\src\cockpit_hud.cpp /Fobuild\ /Fe:native\cockpit_hud.dll /link /NOLOGO /IMPLIB:build\cockpit_hud.lib || exit /b 1
del /q native\cockpit_hud.exp native\variant.txt 2>nul
echo built native\cockpit_hud.dll
