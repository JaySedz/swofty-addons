@echo off
rem Packs the addon as dist\sedz.music-replacer.swoftyaddon (swofty.json at the zip root).
rem Run build.bat first. Install the result by dropping it on the launcher window.
setlocal
cd /d "%~dp0"
if not exist bin\music-replacer-helper.exe (echo bin\music-replacer-helper.exe is missing: run build.bat & exit /b 1)
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\bin\opus
copy /y third_party\opus\opusenc.exe build\pkg\bin\opus\ >nul || exit /b 1
copy /y third_party\opus\LICENSE build\pkg\bin\opus\ >nul
copy /y THIRD_PARTY_NOTICES.md build\pkg\ >nul
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y logo.png build\pkg\ >nul || exit /b 1
copy /y bin\music-replacer-helper.exe build\pkg\bin\ >nul
if not exist dist mkdir dist
if exist dist\sedz.music-replacer.swoftyaddon del /q dist\sedz.music-replacer.swoftyaddon
rem Compress-Archive in Windows PowerShell 5.1 writes "bin\x" entry names; write "bin/x" (the zip standard) instead.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) 'dist\sedz.music-replacer.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed dist\sedz.music-replacer.swoftyaddon
