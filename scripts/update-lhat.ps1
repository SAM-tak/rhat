# Records the L^ commit CI builds lib/lhat's tests against, in scripts/lhat.rev.
# Usage: ./scripts/update-lhat.ps1 [-Rev REF] [-Lhat PATH]
# REF is a commit of the L^ checkout beside this repository (or PATH), HEAD by default, and
# origin/main for the latest pushed. CI fetches it from L^'s remote, so it must be pushed there.
[CmdletBinding()]
param(
    [string]$Rev = 'HEAD',
    [string]$Lhat
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Lhat) { $Lhat = Join-Path (Split-Path -Parent $repoRoot) 'lhat' }
& git -C $Lhat fetch --quiet origin
if ($LASTEXITCODE -ne 0) { throw "Could not fetch L^'s remote in $Lhat." }
$sha = & git -C $Lhat rev-parse --verify --quiet "$Rev^{commit}"
if ($LASTEXITCODE -ne 0 -or -not $sha) { throw "No such commit in ${Lhat}: $Rev" }
if (-not (& git -C $Lhat branch -r --contains $sha)) {
    throw "$sha is not on L^'s remote; push it first."
}
if ($Rev -eq 'HEAD' -and (& git -C $Lhat status --porcelain --untracked-files=no)) {
    Write-Warning "$Lhat has uncommitted changes, which $sha does not have."
}
$file = Join-Path $PSScriptRoot 'lhat.rev'
$old = if (Test-Path -LiteralPath $file) { (Get-Content -LiteralPath $file -Raw).Trim() } else { '' }
if ($old -eq $sha) {
    Write-Output "scripts/lhat.rev is already $sha."
    return
}
[IO.File]::WriteAllText($file, "$sha`n")
Write-Output "scripts/lhat.rev: $(if ($old) { $old } else { '(none)' }) -> $sha"
if ($old) {
    & git -C $Lhat log --oneline "$old..$sha"
}
