# Load the x64 toolchain and R^ paths into the current PowerShell session.
# Usage: . .\scripts\devshell.ps1 [-Configuration Release] [-Clang PATH]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$Clang
)

# Keep helper variables local even when the script is dot-sourced.
& {
    param($Configuration, $Clang, $ScriptDirectory)
    $ErrorActionPreference = 'Stop'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio C++ build tools.'
    }
    $vsRoot = & $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1
    if (-not $vsRoot) { throw 'No Visual Studio installation with the x64 C++ toolchain was found.' }
    # vcvars grows PATH on every run, so a reinitialized session eventually
    # exceeds the cmd.exe line limit; keep an existing x64 environment.
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
        $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
        if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat was not found: $vcvars" }
        $environmentLines = & $env:ComSpec /d /c "`"$vcvars`" 2>&1 && set"
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to initialize the Visual Studio toolchain:`n$($environmentLines -join "`n")"
        }
        foreach ($line in $environmentLines) {
            if ($line -match '^([^=]+)=(.*)$') {
                Set-Item -LiteralPath "Env:\$($matches[1])" -Value $matches[2]
            }
        }
    }

    if (-not $Clang) { $Clang = $env:RHAT_CLANG }
    if (-not $Clang) {
        $clangCommand = Get-Command clang -CommandType Application -ErrorAction SilentlyContinue
        if ($clangCommand) { $Clang = $clangCommand.Source }
    }
    if (-not $Clang) {
        $standalone = Join-Path $env:ProgramFiles 'LLVM\bin\clang.exe'
        $Clang = if (Test-Path -LiteralPath $standalone) {
            $standalone
        } else {
            Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang.exe'
        }
    }
    if (-not (Test-Path -LiteralPath $Clang -PathType Leaf)) {
        throw "Clang was not found: $Clang. Install Clang tools or pass -Clang PATH."
    }
    $env:RHAT_CLANG = (Resolve-Path -LiteralPath $Clang).Path
    $repoRoot = Split-Path -Parent $ScriptDirectory
    $toolPaths = @(
        (Join-Path $repoRoot "build\$Configuration"),
        (Join-Path $repoRoot 'build'),
        (Split-Path -Parent $env:RHAT_CLANG)
    )
    $existing = $env:Path -split ';' | Where-Object { $_ -and $_ -notin $toolPaths }
    $env:Path = ($toolPaths + $existing) -join ';'
    Write-Host "MSVC x64: $((Get-Command cl -CommandType Application).Source)"
    Write-Host "Clang: $env:RHAT_CLANG"
    Write-Host "R^ tools: $($toolPaths[0])"
} $Configuration $Clang $PSScriptRoot
