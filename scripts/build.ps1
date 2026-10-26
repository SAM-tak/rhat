[CmdletBinding()]
param(
    [Alias('Config')][ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Release,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
if ($Release) { $Configuration = 'Release' }
$repoRoot = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\devshell.ps1" -Configuration $Configuration
$clangPath = $env:RHAT_CLANG
$buildPath = Join-Path $repoRoot 'build'
& cmake -S $repoRoot -B $buildPath -DBUILD_TESTING=ON "-DRH_CLANG=$clangPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildPath --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed.' }
if ($Test) {
    & ctest --test-dir $buildPath -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
}
Write-Output "RHAT_CLANG=$clangPath"
