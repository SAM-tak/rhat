Get-CimInstance Win32_Process -Filter "Name='rhat.exe'" |
Where-Object CommandLine -match '\s+lsp(\s|$)' |
ForEach-Object {
    Stop-Process -Id $_.ProcessId -ErrorAction Stop
    Write-Host "Stopped rhat LSP (PID: $($_.ProcessId))"
}
