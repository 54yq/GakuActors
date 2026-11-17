# Opens the separate v0.1 resource backend without touching a game installation.
[CmdletBinding()]
param([string]$Backend = $env:GAKUACTORS_RESOURCE_BACKEND, [string]$Data = $env:GAKUACTORS_RESOURCE_DATA, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
if (!$Backend) {
    $config = Join-Path $PSScriptRoot 'resource-backend.txt'
    if (Test-Path -LiteralPath $config) { $Backend = (Get-Content -LiteralPath $config -Raw).Trim() }
}
if (!$Backend) { $Backend = 'ResourceBackend' }
if (![IO.Path]::IsPathRooted($Backend)) { $Backend = Join-Path $PSScriptRoot $Backend }
$Backend = [IO.Path]::GetFullPath($Backend)
$python = Join-Path $PSScriptRoot 'runtime/python.exe'
if (!(Test-Path -LiteralPath $python -PathType Leaf)) { $python = Join-Path $Backend '.venv/Scripts/python.exe' }
$app = Join-Path $Backend 'app.py'
if (!(Test-Path -LiteralPath $python) -or !(Test-Path -LiteralPath $app)) { throw 'Resource backend or its Python environment is missing. See RESOURCE-LIBRARY.md.' }
if (!$Data) { $Data = Join-Path $env:LOCALAPPDATA 'GakuActors/ResourceLibrary' }
$Data = [IO.Path]::GetFullPath($Data)
New-Item -ItemType Directory -Path $Data -Force | Out-Null
$state = Join-Path $Data 'service.json'
function Get-ListenerId {
    foreach ($line in (& netstat.exe -ano -p tcp)) {
        if ($line -match '^\s*TCP\s+\S+:8777\s+\S+\s+LISTENING\s+(\d+)\s*$') { return [int]$Matches[1] }
    }
    return 0
}
function Test-BackendProcess([int]$id) {
    if (!$id) { return $false }
    $p = Get-CimInstance Win32_Process -Filter "ProcessId = $id" -ErrorAction SilentlyContinue
    return ($p -and $p.CommandLine.Contains($app) -and $p.CommandLine.Contains($Data) -and $p.CommandLine.Contains('--port 8777'))
}
$mutex = New-Object System.Threading.Mutex($false, 'Local\GakuActorsResourceLibrary8777')
if (!$mutex.WaitOne(60000)) { throw 'Resource library is busy starting; try again.' }
try {
    $owned = $false
    if (Test-Path -LiteralPath $state) {
        try {
            $saved = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
            $listenerId = Get-ListenerId
            $owned = $listenerId -eq [int]$saved.pid -and (Test-BackendProcess $listenerId)
        } catch { $owned = $false }
    }
    if (!$owned) {
        if (Get-ListenerId) { throw 'Port 8777 is in use by another process; it was not changed.' }
        $out = Join-Path $Data 'server.out.log'; $err = Join-Path $Data 'server.err.log'
        $process = Start-Process -FilePath $python -ArgumentList @('-u',('"'+$app+'"'),'--data',('"'+$Data+'"'),'--port','8777') -WorkingDirectory $Backend -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
        $ready = $false
        for ($i=0; $i -lt 60; $i++) {
            Start-Sleep -Milliseconds 200
            if ($process.HasExited) { throw "Resource service exited. See $err" }
            $listenerId = Get-ListenerId
            if ($listenerId) {
                $listenerProcess = Get-CimInstance Win32_Process -Filter "ProcessId = $listenerId" -ErrorAction SilentlyContinue
                if ($listenerProcess -and ($listenerId -eq $process.Id -or $listenerProcess.ParentProcessId -eq $process.Id) -and (Test-BackendProcess $listenerId)) { $ready = $true; break }
                throw 'Port 8777 was taken by another process; it was not changed.'
            }
        }
        if (!$ready) { throw "Resource service did not become ready. See $err" }
        @{pid=$listenerId;backend=$Backend;data=$Data;port=8777} | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding UTF8
    }
    $meta = Invoke-RestMethod -Uri 'http://127.0.0.1:8777/api/meta' -TimeoutSec 5
    if ($meta.version -ne '0.1.0' -or [IO.Path]::GetFullPath($meta.data_root) -ne $Data) { throw 'Resource service identity mismatch; no process was changed.' }
    $updater = Join-Path $PSScriptRoot 'Update-ResourceCatalog.py'
if (Test-Path -LiteralPath $updater) {
    # This launcher runs off the UI thread. Failure never prevents opening the saved catalog.
    try { & $python $updater $Backend $Data } catch { Write-Warning 'Automatic catalog check failed; using saved catalog.' }
}
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
if (!$NoBrowser) { Start-Process 'http://127.0.0.1:8777/' }
Write-Output 'Resource library is ready at http://127.0.0.1:8777/'
