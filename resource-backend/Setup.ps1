$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (!(Test-Path -LiteralPath '.venv/Scripts/python.exe')) { & python -m venv .venv; if ($LASTEXITCODE -ne 0) { throw 'Python 3.9 or newer is required' } }
& './.venv/Scripts/python.exe' -m pip install --index-url https://pypi.org/simple -r requirements.txt
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
& './Build-Exporter.ps1'
