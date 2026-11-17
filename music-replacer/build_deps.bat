@echo off
rem Builds libopus (third_party\libopus, the encoder songs are compressed with) as a static /MT library into
rem build\deps (lib\opus.lib + include\opus\*.h). build.bat calls this when build\deps\lib\opus.lib is missing.
rem Run it from a VS x64 prompt (build.bat does); it uses the CMake and Ninja that come with Visual Studio.
setlocal
cd /d "%~dp0"
set OUT=%CD%\build\deps
if not exist "%OUT%\lib" mkdir "%OUT%\lib"
if not exist "%OUT%\include\opus" mkdir "%OUT%\include\opus"

echo == libopus (third_party\libopus)
cmake -S third_party\libopus -B build\libopus -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DOPUS_STATIC_RUNTIME=ON ^
  -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_DEEP_PLC=OFF ^
  -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF ^
  >build\libopus.log 2>&1 || (type build\libopus.log & exit /b 1)
cmake --build build\libopus --target opus >>build\libopus.log 2>&1 || (type build\libopus.log & exit /b 1)
copy /y build\libopus\opus.lib "%OUT%\lib\opus.lib" >nul || exit /b 1
copy /y third_party\libopus\include\*.h "%OUT%\include\opus\" >nul
echo built build\deps
