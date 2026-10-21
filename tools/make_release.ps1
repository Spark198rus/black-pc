# Packs the built loader into a release zip: dist\black-pc-<version>-win32.zip with one folder, port\, holding
# black.exe, blackrt.dll, README.md and LICENSE.txt. Extracted next to the user's game
# folder, port\black.exe finds ..\game and keeps what it writes in port\ (savedata\, black.ini, black.log, cache\).
# Nothing game-derived: the build's black.exe has no icon (tools\set_exe_icon.py only stamps local copies).
# blackrt.pdb and blackrt.map go into a separate -symbols zip, kept to read crash reports of that build.
# Build first: cmake --build loader\build\win32 --config RelWithDebInfo
param([string]$Config = 'RelWithDebInfo')
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content "$root\loader\CMakeLists.txt" -Raw
if ($cmake -notmatch 'project\(\S+ VERSION (\d+\.\d+\.\d+)') { throw 'no project VERSION in loader\CMakeLists.txt' }
$version = $Matches[1]
$bin = "$root\loader\build\win32\$Config"
$name = "black-pc-$version-win32"
$dist = "$root\dist"
New-Item -ItemType Directory -Force $dist | Out-Null

# Staged under %TEMP% so nothing in the project folder has to be deleted.
$stage = Join-Path $env:TEMP "$name-$PID"
$sym = "$stage-symbols"
$port = "$stage\port"
New-Item -ItemType Directory -Force $port, $sym | Out-Null
Copy-Item "$bin\black.exe", "$bin\blackrt.dll" $port
Copy-Item "$root\README.md" "$port\README.md"
Copy-Item "$root\LICENSE" "$port\LICENSE.txt"
Copy-Item "$bin\blackrt.pdb", "$bin\blackrt.map" $sym

Compress-Archive -Path "$stage\*" -DestinationPath "$dist\$name.zip" -Force
Compress-Archive -Path "$sym\*" -DestinationPath "$dist\$name-symbols.zip" -Force
Remove-Item -Recurse -Force $stage, $sym

foreach ($z in "$dist\$name.zip", "$dist\$name-symbols.zip") {
    $bytes = [System.IO.File]::ReadAllBytes($z)
    $sha = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    $h = [System.BitConverter]::ToString($sha).Replace('-', '').ToLower()
    '{0}  {1:N0} bytes  sha256 {2}' -f (Split-Path $z -Leaf), (Get-Item $z).Length, $h
}
