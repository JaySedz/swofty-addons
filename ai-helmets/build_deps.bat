@echo off
rem Builds the static libraries the helper unpacks helmet packs with, into build\deps (lib + include):
rem libarchive (zip, 7z, rar) with zlib, liblzma (xz) and bzip2, all from third_party, all /MT.
rem build.bat calls this when build\deps\lib\archive.lib is missing. Run it from a VS x64 prompt (build.bat does).
setlocal
cd /d "%~dp0"
set OUT=%CD%\build\deps
rem The same folder with forward slashes, for CMake (it reads backslashes as escapes).
set DEPS=%OUT:\=/%
set CM=-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if not exist "%OUT%\lib" mkdir "%OUT%\lib"
if not exist "%OUT%\include" mkdir "%OUT%\include"

echo == zlib (third_party\zlib)
if not exist build\zlib mkdir build\zlib
set ZL=third_party\zlib
cl /nologo /O2 /MT /c /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /Fobuild\zlib\ %ZL%\adler32.c %ZL%\compress.c %ZL%\crc32.c %ZL%\deflate.c %ZL%\gzclose.c %ZL%\gzlib.c %ZL%\gzread.c %ZL%\gzwrite.c %ZL%\infback.c %ZL%\inffast.c %ZL%\inflate.c %ZL%\inftrees.c %ZL%\trees.c %ZL%\uncompr.c %ZL%\zutil.c >build\zlib.log 2>&1 || (type build\zlib.log & exit /b 1)
lib /nologo /OUT:"%OUT%\lib\zlib.lib" build\zlib\*.obj || exit /b 1
copy /y %ZL%\zlib.h "%OUT%\include\" >nul
copy /y %ZL%\zconf.h "%OUT%\include\" >nul

echo == liblzma (third_party\xz)
cmake -S third_party\xz -B build\xz %CM% -DBUILD_TESTING=OFF -DENABLE_NLS=OFF >build\xz.log 2>&1 || (type build\xz.log & exit /b 1)
cmake --build build\xz --target liblzma >>build\xz.log 2>&1 || (type build\xz.log & exit /b 1)
copy /y build\xz\lzma.lib "%OUT%\lib\lzma.lib" >nul
xcopy /y /e /q /i third_party\xz\src\liblzma\api "%OUT%\include" >nul

echo == bzip2 (third_party\bzip2)
if not exist build\bzip2 mkdir build\bzip2
set BZ=third_party\bzip2
cl /nologo /O2 /MT /c /D_CRT_SECURE_NO_WARNINGS /Fobuild\bzip2\ %BZ%\blocksort.c %BZ%\huffman.c %BZ%\crctable.c %BZ%\randtable.c %BZ%\compress.c %BZ%\decompress.c %BZ%\bzlib.c >build\bzip2.log 2>&1 || (type build\bzip2.log & exit /b 1)
lib /nologo /OUT:"%OUT%\lib\bz2.lib" build\bzip2\*.obj || exit /b 1
copy /y %BZ%\bzlib.h "%OUT%\include\" >nul

echo == libarchive (third_party\libarchive)
rem Only the reader and the formats it needs: no tools, tests, crypto, XML or other system libraries.
cmake -S third_party\libarchive -B build\libarchive %CM% -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=OFF ^
  -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_ZSTD=OFF ^
  -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_LIBGCC=OFF -DENABLE_ICONV=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DPOSIX_REGEX_LIB=NONE ^
  -DENABLE_ZLIB=ON -DZLIB_INCLUDE_DIR="%DEPS%/include" -DZLIB_LIBRARY="%DEPS%/lib/zlib.lib" ^
  -DENABLE_LZMA=ON -DLIBLZMA_INCLUDE_DIR="%DEPS%/include" -DLIBLZMA_LIBRARY="%DEPS%/lib/lzma.lib" ^
  -DENABLE_BZip2=ON -DBZIP2_INCLUDE_DIR="%DEPS%/include" -DBZIP2_LIBRARIES="%DEPS%/lib/bz2.lib" -DBZIP2_LIBRARY_RELEASE="%DEPS%/lib/bz2.lib" ^
  >build\libarchive.log 2>&1 || (type build\libarchive.log & exit /b 1)
cmake --build build\libarchive --target archive_static >>build\libarchive.log 2>&1 || (type build\libarchive.log & exit /b 1)
copy /y build\libarchive\libarchive\archive.lib "%OUT%\lib\archive.lib" >nul 2>nul || copy /y build\libarchive\libarchive\archive_static.lib "%OUT%\lib\archive.lib" >nul || exit /b 1
copy /y third_party\libarchive\libarchive\archive.h "%OUT%\include\" >nul
copy /y third_party\libarchive\libarchive\archive_entry.h "%OUT%\include\" >nul
echo built build\deps
