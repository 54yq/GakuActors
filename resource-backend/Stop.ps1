$ErrorActionPreference = 'Stop'
$application = Join-Path $PSScriptRoot 'app.py'
$candidates = @()
$state = Join-Path $PSScriptRoot 'service.json'
if (Test-Path -LiteralPath $state) { $saved = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json; $candidates += [int]$saved.pid }
$listener = Get-NetTCPConnection -LocalPort 8776 -State Listen -ErrorAction SilentlyContinue
if ($listener) { $candidates += $listener.OwningProcess }
foreach ($targetId in ($candidates | Select-Object -Unique)) {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $targetId" -ErrorAction SilentlyContinue
    if ($process -and $process.CommandLine.Contains($application)) { Stop-Process -Id $targetId; Write-Output "Stopped resource downloader PID $targetId" }
}
