$ErrorActionPreference = 'Stop'
$toolDirectory = Join-Path $PSScriptRoot 'tools/AssetStudio'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Missing .NET Framework C# compiler' }
$arguments = @('/nologo','/target:exe','/platform:x64',('/out:' + (Join-Path $toolDirectory 'GakumasModelFbx.exe')),('/r:' + (Join-Path $toolDirectory 'AssetStudio.dll')),('/r:' + (Join-Path $toolDirectory 'AssetStudioUtility.dll')),('/r:' + (Join-Path $toolDirectory 'AssetStudioFBXWrapper.dll')),('/r:' + (Join-Path $toolDirectory 'Newtonsoft.Json.dll')),(Join-Path $PSScriptRoot 'GakumasModelFbx.cs'))
& $compiler $arguments
if ($LASTEXITCODE -ne 0) { throw 'FBX helper build failed' }
Copy-Item -LiteralPath (Join-Path $toolDirectory 'AssetStudioModCLI.exe.config') -Destination (Join-Path $toolDirectory 'GakumasModelFbx.exe.config') -Force
