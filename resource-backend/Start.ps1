$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$state = Join-Path $project 'service.json'
$python = Join-Path $project '.venv/Scripts/python.exe'
$application = Join-Path $project 'app.py'
if (!(Test-Path -LiteralPath $python)) { throw 'Run Setup.ps1 first.' }
if (Test-Path -LiteralPath $state) {
    $saved = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    $previous = Get-CimInstance Win32_Process -Filter "ProcessId = $($saved.pid)" -ErrorAction SilentlyContinue
    if ($previous -and $previous.CommandLine.Contains($application)) { Write-Output 'Already running: http://127.0.0.1:8776/'; return }
}
$listener = Get-NetTCPConnection -LocalPort 8776 -State Listen -ErrorAction SilentlyContinue
if ($listener) { throw 'Port 8776 is occupied; no existing process was changed.' }
$logDirectory = Join-Path $project 'data/logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$service = Start-Process -FilePath $python -ArgumentList @('-u',('"'+$application+'"'),'--port','8776') -WorkingDirectory $project -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logDirectory 'server.out.log') -RedirectStandardError (Join-Path $logDirectory 'server.err.log') -PassThru
@{pid=$service.Id; application=$application; url='http://127.0.0.1:8776/'; startedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
Write-Output 'Started: http://127.0.0.1:8776/'
