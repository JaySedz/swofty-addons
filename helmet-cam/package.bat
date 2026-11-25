@echo off
rem Packs the addon as dist\sedz.helmet-cam.swoftyaddon (swofty.json at the zip root). Run build.bat first.
rem erp\mount.json and tex\strips.png come from tools\gen_mount.py and tools\make_strips.py.
setlocal
cd /d "%~dp0"
if not exist native\helmet_cam.dll (echo native\helmet_cam.dll is missing: run build.bat & exit /b 1)
set "OUT=dist\sedz.helmet-cam.swoftyaddon"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\native build\pkg\fx build\pkg\tex build\pkg\erp
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y icon.png build\pkg\ >nul
copy /y native\helmet_cam.dll build\pkg\native\ >nul
copy /y fx\*.hlsl build\pkg\fx\ >nul
copy /y tex\strips.png build\pkg\tex\ >nul
copy /y erp\mount.json build\pkg\erp\ >nul
if not exist dist mkdir dist
if exist %OUT% del /q %OUT%
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) '%OUT%'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed %OUT%
