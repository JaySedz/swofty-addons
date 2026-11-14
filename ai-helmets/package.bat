@echo off
rem Packs the addon as dist\sedz.ai-helmets.swoftyaddon (swofty.json at the zip root).
rem Run build.bat first. Install the result by dropping it on the launcher window, or with:
rem   swofty addons install dist\sedz.ai-helmets.swoftyaddon
setlocal
cd /d "%~dp0"
if not exist native\ai_helmets.dll (echo native\ai_helmets.dll is missing: run build.bat & exit /b 1)
if not exist bin\ai-helmets-helper.exe (echo bin\ai-helmets-helper.exe is missing: run build.bat & exit /b 1)
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\native build\pkg\bin
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y THIRD_PARTY_NOTICES.md build\pkg\ >nul
copy /y icon.png build\pkg\ >nul
copy /y native\ai_helmets.dll build\pkg\native\ >nul
copy /y bin\ai-helmets-helper.exe build\pkg\bin\ >nul
if not exist dist mkdir dist
if exist dist\sedz.ai-helmets.swoftyaddon del /q dist\sedz.ai-helmets.swoftyaddon
rem Compress-Archive in Windows PowerShell 5.1 writes "bin\x" entry names; write "bin/x" (the zip standard) instead.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) 'dist\sedz.ai-helmets.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed dist\sedz.ai-helmets.swoftyaddon
