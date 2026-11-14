# Builds the L^ libraries tests/lhat_host.py links -- the core, its standard library and ports --
# into build/release of the L^ checkout beside this repository (or PATH), with L^'s own release
# preset. Only those targets are built, and no option is changed, so a local L^ build there is
# left as it was configured.
# Usage: ./scripts/build-lhat.ps1 [-Lhat PATH]
[CmdletBinding()]
param([string]$Lhat)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Lhat) { $Lhat = Join-Path (Split-Path -Parent $repoRoot) 'lhat' }
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    . "$PSScriptRoot\devshell.ps1"
}
Push-Location -LiteralPath $Lhat
try {
    & cmake --preset release
    if ($LASTEXITCODE -ne 0) { throw 'L^ configuration failed.' }
    & cmake --build --preset release --target lhat lhatstdlib lhatport lhatthread
    if ($LASTEXITCODE -ne 0) { throw 'L^ build failed.' }
} finally {
    Pop-Location
}
