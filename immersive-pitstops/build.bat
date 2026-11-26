@echo off
rem Builds native\immersive_pitstops.dll and bin\immersive-pitstops-helper.exe (the Pit Stops page). Needs Visual Studio 2022 or later with "Desktop development with C++".
rem The pit stop sequences in erp\ and the op files are made by tools\make_stuck_nut.py and tools\make_ops.py (Python 3).
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
set FLAGS=/nologo /utf-8 /std:c++latest /Zc:preprocessor /permissive- /EHsc /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN
cl %FLAGS% /LD /Inative\include native\src\immersive_pitstops.cpp /Fobuild\ /Fe:native\immersive_pitstops.dll /link /NOLOGO /IMPLIB:build\immersive_pitstops.lib || exit /b 1
del /q native\immersive_pitstops.exp 2>nul
rem 1.0.42: the Pit Stops page (a launcher plugin; the same protocol as AI Helmets' helper)
if not exist bin mkdir bin
cl %FLAGS% /Inative\include helper\src\main.cpp /Fobuild\ /Fe:bin\immersive-pitstops-helper.exe /link /NOLOGO /SUBSYSTEM:CONSOLE || exit /b 1
echo built native\immersive_pitstops.dll and bin\immersive-pitstops-helper.exe
