@echo off
rem Packs the addon as dist\sedz.race-dash.swoftyaddon (swofty.json at the zip root).
rem Run tools\build_ui.py and build.bat first.
setlocal
cd /d "%~dp0"
if not exist native\race_dash.dll (echo native\race_dash.dll is missing: run build.bat & exit /b 1)
if not exist ui\lcd_display.idf (echo ui\lcd_display.idf is missing: run tools\build_ui.py & exit /b 1)
set "OUT=dist\sedz.race-dash.swoftyaddon"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\native build\pkg\ui build\pkg\erp
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y native\race_dash.dll build\pkg\native\ >nul
copy /y ui\lcd_display.idf build\pkg\ui\ >nul
copy /y ui\race_assets_f1.ui build\pkg\ui\ >nul
copy /y erp\race_dash.json build\pkg\erp\ >nul
if not exist dist mkdir dist
if exist %OUT% del /q %OUT%
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) '%OUT%'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed %OUT%
