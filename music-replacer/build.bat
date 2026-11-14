@echo off
rem Builds bin\music-replacer-helper.exe (the launcher page). Needs Visual Studio 2022 (or Build Tools) with
rem "Desktop development with C++".
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo Could not find vcvars64.bat. Install Visual Studio with "Desktop development with C++".
    exit /b 1
)
call "%VCVARS%" >nul 2>nul
cd /d "%~dp0"
if not exist build mkdir build
if not exist bin mkdir bin
cl /nologo /utf-8 /std:c++latest /Zc:preprocessor /permissive- /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Iinclude helper\src\main.cpp /Fobuild\ /Fe:bin\music-replacer-helper.exe /link /NOLOGO /SUBSYSTEM:CONSOLE || exit /b 1
if not exist bin\opus mkdir bin\opus
copy /y third_party\opus\opusenc.exe bin\opus\ >nul
echo built bin\music-replacer-helper.exe
