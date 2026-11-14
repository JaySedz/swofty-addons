@echo off
rem Builds tools\erptool.exe (development aid; not shipped in the addon).
setlocal
cd /d "%~dp0.."
set "VC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VC%" set "VC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VC%" >nul || exit /b 1
if not exist build\zstd mkdir build\zstd
set ZFILES=third_party\zstd\lib\common\debug.c third_party\zstd\lib\common\entropy_common.c third_party\zstd\lib\common\error_private.c third_party\zstd\lib\common\fse_decompress.c third_party\zstd\lib\common\xxhash.c third_party\zstd\lib\common\zstd_common.c third_party\zstd\lib\decompress\huf_decompress.c third_party\zstd\lib\decompress\zstd_ddict.c third_party\zstd\lib\decompress\zstd_decompress.c third_party\zstd\lib\decompress\zstd_decompress_block.c
cl /nologo /O2 /MT /c /DZSTD_DISABLE_ASM /Fobuild\zstd\ %ZFILES% || exit /b 1
cl /nologo /utf-8 /std:c++latest /EHsc /O2 /MT /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS tools\erptool.cpp build\zstd\*.obj bcrypt.lib /Fe:tools\erptool.exe /Fo:build\ || exit /b 1
echo built tools\erptool.exe
