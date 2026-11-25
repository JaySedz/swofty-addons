@echo off
rem Packs dist\sedz.ferrari-screen-fix.swoftyaddon. Run tools\build_fix.py first.
setlocal
cd /d "%~dp0"
if not exist erp\vb9.bin (echo erp\vb9.bin is missing: run tools\build_fix.py & exit /b 1)
set "OUT=dist\sedz.ferrari-screen-fix.swoftyaddon"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\erp
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y erp\vb9.bin build\pkg\erp\ >nul
copy /y erp\ferrari_screen_fix.json build\pkg\erp\ >nul
if not exist dist mkdir dist
if exist %OUT% del /q %OUT%
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) '%OUT%'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed %OUT%
