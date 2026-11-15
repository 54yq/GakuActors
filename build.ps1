#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('All','Runtime','Tests')][string]$Target='All',
    [string]$OutputRoot='',
    [string]$PortableVisualStudio='',
    [string]$WindowsSdkRoot='',
    [string]$RustDesktopExe='',
    [string]$Cmake='',
    [string]$ConanToolchain='',
    [string]$CmakeVisualStudio=''
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
$env:INCLUDE=@((Join-Path $toolset.FullName 'include'),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/ucrt"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/um"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/shared"),(Join-Path $WindowsSdkRoot "Include/$($sdk.Name)/winrt")) -join ';'
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
        & $compiler @common (Join-Path $repo 'tests/core_tests.cpp') "/Fo$OutputRoot/obj/tests.obj" "/Fe$OutputRoot/core_tests.exe" ole32.lib windowscodecs.lib crypt32.lib
        if($LASTEXITCODE -ne 0){throw 'Core tests did not compile.'}
        & (Join-Path $OutputRoot 'core_tests.exe')
        if($LASTEXITCODE -ne 0){throw 'Core tests failed.'}
        $uiSources=@('vendor/imgui/imgui.cpp','vendor/imgui/imgui_draw.cpp','vendor/imgui/imgui_widgets.cpp','vendor/imgui/imgui_tables.cpp') | ForEach-Object {Join-Path $repo $_}
        New-Item -ItemType Directory -Path (Join-Path $OutputRoot 'startup-obj') -Force | Out-Null
        & $compiler @common (Join-Path $repo 'tests/startup_policy_tests.cpp') @uiSources "/Fo$OutputRoot/startup-obj/" "/Fe$OutputRoot/startup_policy_tests.exe" user32.lib comdlg32.lib ole32.lib windowscodecs.lib crypt32.lib
        if($LASTEXITCODE -ne 0){throw 'Startup policy tests did not compile.'}
        & (Join-Path $OutputRoot 'startup_policy_tests.exe')
        if($LASTEXITCODE -ne 0){throw 'Startup policy tests failed.'}
        & $compiler @common (Join-Path $repo 'tests/desktop_transport_child.cpp') "/Fo$OutputRoot/obj/transport-child.obj" "/Fe$OutputRoot/desktop_transport_child.exe"
        if($LASTEXITCODE -ne 0){throw 'Desktop transport child did not compile.'}
        & $compiler @common (Join-Path $repo 'tests/desktop_transport_tests.cpp') "/Fo$OutputRoot/obj/transport-test.obj" "/Fe$OutputRoot/desktop_transport_tests.exe"
        if($LASTEXITCODE -ne 0){throw 'Desktop transport test did not compile.'}
        & $compiler @common (Join-Path $repo 'tests/desktop_mock_host.cpp') "/Fo$OutputRoot/obj/desktop-mock.obj" "/Fe$OutputRoot/desktop_mock_host.exe"
        if($LASTEXITCODE -ne 0){throw 'Desktop mock host did not compile.'}
        & (Join-Path $OutputRoot 'desktop_transport_tests.exe') (Join-Path $OutputRoot 'desktop_transport_child.exe')
        if($LASTEXITCODE -ne 0){throw 'Desktop transport round trip failed.'}
        if($RustDesktopExe){
            & $compiler @common (Join-Path $repo 'tests/desktop_rust_transport_tests.cpp') "/Fo$OutputRoot/obj/transport-rust.obj" "/Fe$OutputRoot/desktop_rust_transport_tests.exe"
            if($LASTEXITCODE -ne 0){throw 'Rust desktop transport test did not compile.'}
            & (Join-Path $OutputRoot 'desktop_rust_transport_tests.exe') $RustDesktopExe
            if($LASTEXITCODE -ne 0){throw 'Rust desktop transport round trip failed.'}
        }
    }
    if($Target -in @('All','Runtime')){
        if(-not $Cmake){$command=Get-Command cmake -ErrorAction SilentlyContinue;if($command){$Cmake=$command.Source}}
        if(-not $Cmake -or -not $ConanToolchain){throw 'The integrated plugin requires -Cmake and -ConanToolchain (generated from camera-core/hosted/conanfile.py).'}
        if(-not $CmakeVisualStudio){$CmakeVisualStudio=$PortableVisualStudio}
        $integratedBuild=Join-Path $OutputRoot 'integrated-cmake'
        & (Join-Path $repo 'build-camera-core.ps1') -Cmake $Cmake -ConanToolchain $ConanToolchain -VisualStudio $CmakeVisualStudio -OutputRoot $integratedBuild
        $binary=Join-Path $integratedBuild 'artifacts/hosted/Release/GakuActors.dll'
        Copy-Item -LiteralPath $binary -Destination (Join-Path $OutputRoot 'GakuActors.dll') -Force
        Copy-Item -LiteralPath (Join-Path $repo 'camera-core/LICENSE') -Destination (Join-Path $OutputRoot 'GakuActors-GPL-3.0.txt') -Force
    }
    if($Target -in @('All','Tests') -and (Test-Path (Join-Path $OutputRoot 'GakuActors.dll'))){
        & $compiler @common (Join-Path $repo 'tests/abi_tests.cpp') "/Fo$OutputRoot/obj/abi.obj" "/Fe$OutputRoot/abi_tests.exe"
        if($LASTEXITCODE -ne 0){throw 'ABI tests did not compile.'}
        & (Join-Path $OutputRoot 'abi_tests.exe') (Join-Path $OutputRoot 'GakuActors.dll')
        if($LASTEXITCODE -ne 0){throw 'ABI tests failed.'}
    }
} finally {Pop-Location}
Write-Host 'GakuActors local build completed. No game files were modified.'
