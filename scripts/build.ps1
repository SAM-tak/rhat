param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$clangCommand = Get-Command clang -ErrorAction SilentlyContinue
if ($clangCommand) {
    $clangPath = $clangCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $clangPath = & $vswhere -latest -products '*' -find 'VC\Tools\Llvm\x64\bin\clang.exe' |
            Select-Object -First 1
    }
}
if (-not $clangPath) { throw 'Clang was not found. Install LLVM or the Visual Studio Clang tools.' }
$env:RHAT_CLANG = $clangPath
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
