@echo off
rem Builds native\ai_helmets.dll (the in-game plugin) and bin\ai-helmets-helper.exe (the launcher page).
rem Needs Visual Studio 2022 (or Build Tools) with "Desktop development with C++".
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

set FLAGS=/nologo /utf-8 /std:c++latest /Zc:preprocessor /permissive- /EHsc /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN

echo == native\ai_helmets.dll
cl %FLAGS% /LD /Inative\include native\src\ai_helmets.cpp /Fobuild\ /Fe:native\ai_helmets.dll /link /NOLOGO /IMPLIB:build\ai_helmets.lib || exit /b 1

echo == zstd decoder (third_party\zstd, BSD)
if not exist build\zstd mkdir build\zstd
set ZDIR=third_party\zstd\lib
cl /nologo /O2 /MT /c /DZSTD_DISABLE_ASM /Fobuild\zstd\ %ZDIR%\common\debug.c %ZDIR%\common\entropy_common.c %ZDIR%\common\error_private.c %ZDIR%\common\fse_decompress.c %ZDIR%\common\xxhash.c %ZDIR%\common\zstd_common.c %ZDIR%\decompress\huf_decompress.c %ZDIR%\decompress\zstd_ddict.c %ZDIR%\decompress\zstd_decompress.c %ZDIR%\decompress\zstd_decompress_block.c >nul || exit /b 1

echo == libarchive with zlib, liblzma and bzip2 (third_party, see THIRD_PARTY_NOTICES.md; built once, needs CMake from VS)
if not exist build\deps\lib\archive.lib call "%~dp0build_deps.bat" || exit /b 1

echo == bin\ai-helmets-helper.exe
cl %FLAGS% /Inative\include /Ibuild\deps\include helper\src\main.cpp build\zstd\*.obj build\deps\lib\archive.lib build\deps\lib\lzma.lib build\deps\lib\bz2.lib build\deps\lib\zlib.lib advapi32.lib bcrypt.lib /Fobuild\ /Fe:bin\ai-helmets-helper.exe /link /NOLOGO /SUBSYSTEM:CONSOLE || exit /b 1

del /q native\ai_helmets.exp 2>nul
echo built native\ai_helmets.dll and bin\ai-helmets-helper.exe
