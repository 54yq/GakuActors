#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('All','Runtime','Tests')][string]$Target='All',
    [string]$OutputRoot='',
    [string]$PortableVisualStudio='',
    [string]$WindowsSdkRoot=''
)
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
if(-not $OutputRoot){$OutputRoot=Join-Path $repo 'build'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(-not $PortableVisualStudio){
    $finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(Test-Path $finder){$PortableVisualStudio=& $finder -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath}
}
if(-not $PortableVisualStudio){throw 'Pass -PortableVisualStudio or install the MSVC x64 toolset.'}
$toolset=Get-ChildItem -LiteralPath (Join-Path $PortableVisualStudio 'VC/Tools/MSVC') -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if(-not $WindowsSdkRoot){$WindowsSdkRoot=(Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10}
if(-not $WindowsSdkRoot){throw 'Pass -WindowsSdkRoot.'}
$sdk=Get-ChildItem -LiteralPath (Join-Path $WindowsSdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if(-not $toolset -or -not $sdk){throw 'MSVC or Windows SDK inputs missing.'}
$compiler=Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
$env:INCLUDE=@((Join-Path $toolset.FullName 'include'),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/ucrt"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/um"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/shared")) -join ';'
$env:LIB=@((Join-Path $toolset.FullName 'lib/x64'),(Join-Path $WindowsSdkRoot "Lib/$($sdk.Name)/ucrt/x64"),(Join-Path $WindowsSdkRoot "Lib/$($sdk.Name)/um/x64")) -join ';'
$env:PATH=(Join-Path $toolset.FullName 'bin/Hostx64/x64')+';'+$env:PATH
New-Item -ItemType Directory -Path $OutputRoot,(Join-Path $OutputRoot 'obj') -Force | Out-Null
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/DNOMINMAX',"/I$repo/include","/I$repo/src","/I$repo/vendor","/I$repo/vendor/imgui")
Push-Location -LiteralPath $OutputRoot
try {
    if($Target -in @('All','Tests')){
        $isolatedTemp=Join-Path $OutputRoot 'test-temp'
        New-Item -ItemType Directory -Path $isolatedTemp -Force | Out-Null
        $env:TEMP=$isolatedTemp;$env:TMP=$isolatedTemp
        & $compiler @common (Join-Path $repo 'tests/core_tests.cpp') "/Fo$OutputRoot/obj/tests.obj" "/Fe$OutputRoot/core_tests.exe"
        if($LASTEXITCODE -ne 0){throw 'Core tests did not compile.'}
        & (Join-Path $OutputRoot 'core_tests.exe')
        if($LASTEXITCODE -ne 0){throw 'Core tests failed.'}
        $uiSources=@('vendor/imgui/imgui.cpp','vendor/imgui/imgui_draw.cpp','vendor/imgui/imgui_widgets.cpp','vendor/imgui/imgui_tables.cpp') | ForEach-Object {Join-Path $repo $_}
        New-Item -ItemType Directory -Path (Join-Path $OutputRoot 'startup-obj') -Force | Out-Null
        & $compiler @common (Join-Path $repo 'tests/startup_policy_tests.cpp') @uiSources "/Fo$OutputRoot/startup-obj/" "/Fe$OutputRoot/startup_policy_tests.exe" user32.lib
        if($LASTEXITCODE -ne 0){throw 'Startup policy tests did not compile.'}
        & (Join-Path $OutputRoot 'startup_policy_tests.exe')
        if($LASTEXITCODE -ne 0){throw 'Startup policy tests failed.'}
    }
    if($Target -in @('All','Runtime')){
        $sources=@('src/Plugin.cpp','vendor/imgui/imgui.cpp','vendor/imgui/imgui_draw.cpp','vendor/imgui/imgui_widgets.cpp','vendor/imgui/imgui_tables.cpp') | ForEach-Object {Join-Path $repo $_}
        & $compiler @common /LD /O2 @sources "/Fo$OutputRoot/obj/" user32.lib /link "/OUT:$OutputRoot/GakuActors.dll"
        if($LASTEXITCODE -ne 0){throw 'Plugin build failed.'}
    }
    if($Target -in @('All','Tests') -and (Test-Path (Join-Path $OutputRoot 'GakuActors.dll'))){
        & $compiler @common (Join-Path $repo 'tests/abi_tests.cpp') "/Fo$OutputRoot/obj/abi.obj" "/Fe$OutputRoot/abi_tests.exe"
        if($LASTEXITCODE -ne 0){throw 'ABI tests did not compile.'}
        & (Join-Path $OutputRoot 'abi_tests.exe') (Join-Path $OutputRoot 'GakuActors.dll')
        if($LASTEXITCODE -ne 0){throw 'ABI tests failed.'}
    }
} finally {Pop-Location}
Write-Host 'GakuActors local build completed. No game files were modified.'
