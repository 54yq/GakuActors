#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Cmake,
    [Parameter(Mandatory=$true)][string]$ConanToolchain,
    [Parameter(Mandatory=$true)][string]$VisualStudio,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $Cmake -S (Join-Path $repo 'camera-core/hosted') -B $output -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_GENERATOR_INSTANCE=$VisualStudio" "-DCMAKE_TOOLCHAIN_FILE=$ConanToolchain" "-DLOCALIFY_ARTIFACT_ROOT=$output/artifacts"
if($LASTEXITCODE -ne 0){throw 'Camera core CMake configuration failed.'}
& $Cmake --build $output --config Release --target GakuActors --parallel 4
if($LASTEXITCODE -ne 0){throw 'Integrated GakuActors build failed.'}
$binary=Join-Path $output 'artifacts/hosted/Release/GakuActors.dll'
if(-not (Test-Path -LiteralPath $binary -PathType Leaf)){throw 'Integrated plugin was not produced.'}
Write-Host "Integrated GakuActors: $binary"
