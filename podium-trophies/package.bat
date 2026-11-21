@echo off
rem Builds Podium Trophies: the engine's own trophy pack (pack\, from trophies.json and the game's files; Python 3
rem with numpy and Pillow, game closed), the launcher helper (bin\podium-trophies.exe; Rust), then packs
rem dist\sedz.podium-trophies.swoftyaddon. F1_25 = the game folder if it is not E:\Steam\steamapps\common\F1 25.
setlocal
cd /d "%~dp0"
python tools\build_pack.py trophies.json pack || exit /b 1
cargo build --release --manifest-path helper\Cargo.toml || exit /b 1
if not exist bin mkdir bin
copy /y helper\target\release\podium-trophies.exe bin\ >nul || exit /b 1
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg
copy /y swofty.json build\pkg\ >nul
copy /y README.md build\pkg\ >nul
copy /y LICENSE build\pkg\ >nul
xcopy /e /i /q /y pack build\pkg\pack >nul
xcopy /e /i /q /y bin build\pkg\bin >nul
if not exist dist mkdir dist
if exist dist\sedz.podium-trophies.swoftyaddon del /q dist\sedz.podium-trophies.swoftyaddon
rem Write "pack/x" entry names (the zip standard), not the "pack\x" that Compress-Archive in PowerShell 5.1 writes.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'build\pkg').Path; $zip = [IO.Compression.ZipFile]::Open((Join-Path (Get-Location) 'dist\sedz.podium-trophies.swoftyaddon'), 'Create'); try { Get-ChildItem -Path $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name) } } finally { $zip.Dispose() }" || exit /b 1
echo packed dist\sedz.podium-trophies.swoftyaddon
