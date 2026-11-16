#Requires -Version 5.1
[CmdletBinding()]
param([string]$Cargo='cargo',[string]$OutputRoot='', [switch]$Offline)
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
if(-not $OutputRoot){$OutputRoot=Join-Path $repo 'build/slint-desktop'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
$manifest=Join-Path $repo 'desktop-rust/Cargo.toml'
$cargoArgs=@('build','--release','--locked','--manifest-path',$manifest)
if($Offline){$cargoArgs+='--offline'}
& $Cargo @cargoArgs
if($LASTEXITCODE -ne 0){throw 'Slint desktop build failed.'}
$binary=Join-Path $repo 'desktop-rust/target/release/gakuactors-desktop.exe'
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $OutputRoot 'GakuActors.Desktop.exe') -Force
Copy-Item -LiteralPath (Join-Path $repo 'desktop-rust/Cargo.lock') -Destination $OutputRoot -Force
Copy-Item -LiteralPath (Join-Path $repo 'camera-core/LICENSE') -Destination (Join-Path $OutputRoot 'GakuActors.Desktop-GPL-3.0.txt') -Force
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $OutputRoot 'GakuActors-source-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $repo 'THIRD_PARTY_NOTICES.md') -Destination $OutputRoot -Force
$licenseRoot=Join-Path $OutputRoot 'GakuActors.Desktop.licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'desktop-rust/licenses') -Destination $licenseRoot -Recurse -Force
$metadataArgs=@('metadata','--format-version','1','--locked','--manifest-path',$manifest,'--filter-platform','x86_64-pc-windows-msvc')
if($Offline){$metadataArgs+='--offline'}
$metadataText=& $Cargo @metadataArgs
if($LASTEXITCODE -ne 0){throw 'Dependency metadata collection failed.'}
$metadata=$metadataText | ConvertFrom-Json
$inventory=@()
foreach($package in $metadata.packages){
    $inventory+=[pscustomobject]@{name=$package.name;version=$package.version;license=$package.license;source=$package.source}
    $packageRoot=Split-Path -Parent $package.manifest_path
    $notices=Get-ChildItem -LiteralPath $packageRoot -File | Where-Object {$_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE|AUTHORS)'}
    if($notices){
        $packageOut=Join-Path $licenseRoot ($package.name+'-'+$package.version)
        New-Item -ItemType Directory -Path $packageOut -Force | Out-Null
        foreach($notice in $notices){Copy-Item -LiteralPath $notice.FullName -Destination $packageOut -Force}
    }
    foreach($folder in @('LICENSES','licenses')){
        $noticeFolder=Join-Path $packageRoot $folder
        if(Test-Path -LiteralPath $noticeFolder -PathType Container){
            $packageOut=Join-Path $licenseRoot ($package.name+'-'+$package.version)
            New-Item -ItemType Directory -Path $packageOut -Force | Out-Null
            Copy-Item -LiteralPath $noticeFolder -Destination $packageOut -Recurse -Force
            break
        }
    }
}
$inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $licenseRoot 'dependencies.json') -Encoding UTF8
Write-Host "Slint desktop package: $OutputRoot"
Write-Host 'No game files were modified; no GUI was launched.'
