# Run inside the Windows (x64 or Windows-on-ARM) VM.
# Copies fm1_open from the shared Mac folder to a local NTFS path (the SDK build
# rejects UNC/mapped paths in several checks and is slow over shares), runs the
# official firmware-only build, and copies the results back to ../out/ on the Mac.
#
#   powershell -ExecutionPolicy Bypass -File \\Mac\Home\Documents\m-vave-fm1\development\windows\build-firmware.ps1
#
[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\fm1_open'),
    [string]$Work = 'C:\fm1\fm1_open',
    [string]$Toolchain = $(if ($env:FM1_PI32_TOOLCHAIN) { $env:FM1_PI32_TOOLCHAIN } else { 'C:\JL\pi32' }),
    [string]$Out = (Join-Path $PSScriptRoot '..\out')
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path (Join-Path $Toolchain 'bin\clang.exe'))) {
    throw "JieLi toolchain 2.5.3 not found at $Toolchain (see docs/TOOLCHAIN.md)"
}
New-Item -ItemType Directory -Force -Path $Work, $Out | Out-Null
# Mirror sources, keep the build cache on the VM side.
robocopy $Source $Work /MIR /XD .git build /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
Push-Location $Work
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File .\sdk\Build.ps1 -FirmwareOnly -Toolchain $Toolchain
    if ($LASTEXITCODE -ne 0) { throw "Build.ps1 failed ($LASTEXITCODE)" }
} finally { Pop-Location }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$dest = Join-Path $Out $stamp
New-Item -ItemType Directory -Force -Path $dest | Out-Null
foreach ($f in @('build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc','build\dx7\build-identity.json',
                 'build\dx7\sdk.map','build\dx7\sdk.elf','build\dx7\firmware-report.json',
                 'build\dx7\package-report.json','build\dx7\compile_commands.json')) {
    $p = Join-Path $Work $f
    if (Test-Path $p) { Copy-Item $p $dest }
}
Copy-Item (Join-Path $dest 'FM1_OFFICIAL_OPEN.fwsc') (Join-Path $Out 'latest.fwsc') -Force
Copy-Item (Join-Path $dest 'build-identity.json') (Join-Path $Out 'latest-build-identity.json') -Force
Write-Host "Firmware: $dest"
Write-Host "Flash from macOS:  python3 tools/fm1tool.py flash out/latest.fwsc --identity-json out/latest-build-identity.json"
