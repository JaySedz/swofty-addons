@echo off
rem Packs the addon as dist\sedz.immersive-pitstops.swoftyaddon (swofty.json at the zip root).
rem Install the result by dropping it on the launcher window, or with:
rem   swofty addons install dist\sedz.immersive-pitstops.swoftyaddon
setlocal
cd /d "%~dp0"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\db
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y db\pitstops.json build\pkg\db\ >nul
if not exist dist mkdir dist
if exist dist\sedz.immersive-pitstops.swoftyaddon del /q dist\sedz.immersive-pitstops.swoftyaddon
rem Write "db/x" entry names (the zip standard), not the "db\x" that Compress-Archive in PowerShell 5.1 writes.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) 'dist\sedz.immersive-pitstops.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed dist\sedz.immersive-pitstops.swoftyaddon
