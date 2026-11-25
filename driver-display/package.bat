@echo off
rem Packs the addon as dist\sedz.driver-display.swoftyaddon (swofty.json at the zip root).
rem Run tools\build_ui.py and build.bat first.
setlocal
cd /d "%~dp0"
if not exist native\driver_display.dll (echo native\driver_display.dll is missing: run build.bat & exit /b 1)
if not exist ui\lcd_display.idf (echo ui\lcd_display.idf is missing: run tools\build_ui.py & exit /b 1)
set "OUT=dist\sedz.driver-display.swoftyaddon"
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg\native build\pkg\ui build\pkg\ui\tex build\pkg\erp
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
copy /y icon.png build\pkg\ >nul
copy /y native\driver_display.dll build\pkg\native\ >nul
copy /y ui\lcd_display.idf build\pkg\ui\ >nul
copy /y ui\race_assets_f1.ui build\pkg\ui\ >nul
copy /y ui\tex\* build\pkg\ui\tex\ >nul
rem 1.3.8: colour clips (a text's colour from its animation clip), when the build made any
if exist ui\clips\* (mkdir build\pkg\ui\clips & copy /y ui\clips\* build\pkg\ui\clips\ >nul)
copy /y erp\driver_display.json build\pkg\erp\ >nul
copy /y erp\*.bin build\pkg\erp\ >nul
rem 1.3.5: every file the launcher ops name must be in the package (1.3.2-1.3.4 left out erp\ferrari25_screen_vb3.bin)
powershell -NoProfile -Command "$ops = (Get-Content -Raw 'erp\driver_display.json' | ConvertFrom-Json).ops; $miss = @(); foreach ($o in $ops) { foreach ($f in @($o.source) + @($o.sources)) { if ($f -and $f -is [string] -and -not (Test-Path (Join-Path 'build\pkg' $f))) { $miss += $f } } }; if ($miss.Count) { Write-Host ('missing from the package: ' + (($miss | Select-Object -Unique) -join ', ')); exit 1 }" || exit /b 1
if not exist dist mkdir dist
if exist %OUT% del /q %OUT%
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) '%OUT%'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed %OUT%
