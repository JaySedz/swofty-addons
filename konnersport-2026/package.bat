@echo off
rem Packs the addon as dist\sedz.konnersport-2026.swoftyaddon (swofty.json at the zip root).
rem Install the result by dropping it on the launcher window, or with:
rem   swofty addons install dist\sedz.konnersport-2026.swoftyaddon
setlocal
cd /d "%~dp0"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\db
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y db\konnersport.json build\pkg\db\ >nul
if not exist dist mkdir dist
if exist dist\sedz.konnersport-2026.swoftyaddon del /q dist\sedz.konnersport-2026.swoftyaddon
rem Write "db/x" entry names (the zip standard), not the "db\x" that Compress-Archive in PowerShell 5.1 writes.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) 'dist\sedz.konnersport-2026.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed dist\sedz.konnersport-2026.swoftyaddon
