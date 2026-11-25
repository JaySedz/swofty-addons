@echo off
rem Builds native\helmet_cam.dll. Needs Visual Studio 2022 or later with "Desktop development with C++".
rem The visor pass (fx\visor.hlsl) is compiled by the launcher when it builds the profile.
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
set FLAGS=/nologo /utf-8 /std:c++latest /Zc:preprocessor /permissive- /EHsc /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN
cl %FLAGS% /LD /Inative\include native\src\helmet_cam.cpp /Fobuild\ /Fe:native\helmet_cam.dll /link /NOLOGO /IMPLIB:build\helmet_cam.lib || exit /b 1
del /q native\helmet_cam.exp 2>nul
echo built native\helmet_cam.dll
