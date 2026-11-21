@echo off
rem Builds the limit-test addon sedz.trophy-test-pack (a trophy pack only) into dist\. Game closed.
setlocal
cd /d "%~dp0"
if exist addon rmdir /s /q addon
mkdir addon
python ..\tools\build_pack.py trophies.json addon\podium-trophies || exit /b 1
copy /y swofty.json addon\ >nul
copy /y registry.json addon\ >nul
if not exist ..\dist mkdir ..\dist
if exist ..\dist\sedz.trophy-test-pack.swoftyaddon del /q ..\dist\sedz.trophy-test-pack.swoftyaddon
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'addon').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Resolve-Path '..\dist') 'sedz.trophy-test-pack.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed ..\dist\sedz.trophy-test-pack.swoftyaddon
