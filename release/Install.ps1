[CmdletBinding()]
param([string]$GameRoot,[switch]$DesktopShortcuts)
$ErrorActionPreference='Stop'
if(!$GameRoot){$GameRoot=Read-Host '请输入包含 gakumas.exe 的游戏目录'}
$GameRoot=[IO.Path]::GetFullPath($GameRoot.Trim().Trim('"'))
if(!(Test-Path -LiteralPath (Join-Path $GameRoot 'gakumas.exe') -PathType Leaf)){throw '未找到 gakumas.exe，请检查游戏目录。'}
if(Get-Process -Name 'gakumas','GakuActors.Desktop' -ErrorAction SilentlyContinue){throw '请先关闭游戏和 GakuActors 桌面程序。'}
$payload=Join-Path $PSScriptRoot 'payload'
$loaderSource=Join-Path $PSScriptRoot 'loader\version.dll'
$loaderTarget=Join-Path $GameRoot 'version.dll'
$loaderDir=Join-Path $GameRoot 'GakuLoader'
if((Test-Path -LiteralPath $loaderTarget) -and !(Test-Path -LiteralPath $loaderDir -PathType Container)){throw '游戏目录已有 version.dll，但未找到 GakuLoader 目录。请确认现有加载器后再安装。'}
if(!(Test-Path -LiteralPath $loaderTarget) -and !(Test-Path -LiteralPath $loaderSource -PathType Leaf)){throw '缺少 GakuLoader 文件。'}
$backup=Join-Path $loaderDir ('Backups\GakuActors\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if(Test-Path -LiteralPath $backup){throw '备份目录已存在。'}
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$files=Get-ChildItem -LiteralPath $payload -File -Recurse
$completed=New-Object 'System.Collections.Generic.List[object]'
try {
    foreach($file in $files){
        $relative=$file.FullName.Substring($payload.Length).TrimStart('\')
        $dest=Join-Path $GameRoot $relative
        $save=Join-Path $backup $relative
        $existed=Test-Path -LiteralPath $dest -PathType Leaf
        if($existed){New-Item -ItemType Directory -Path (Split-Path -Parent $save) -Force | Out-Null;Copy-Item -LiteralPath $dest -Destination $save}
        $completed.Add([pscustomobject]@{Destination=$dest;Backup=$save;Existed=$existed})
        New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
        if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $dest).Hash){throw "文件校验失败：$relative"}
    }
    if(!(Test-Path -LiteralPath $loaderTarget)){
        $completed.Add([pscustomobject]@{Destination=$loaderTarget;Backup='';Existed=$false})
        Copy-Item -LiteralPath $loaderSource -Destination $loaderTarget
        if((Get-FileHash -LiteralPath $loaderSource).Hash -ne (Get-FileHash -LiteralPath $loaderTarget).Hash){throw 'GakuLoader 校验失败。'}
    }
    $completed | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $backup 'restore.json') -Encoding UTF8
    $shortcut=Join-Path $GameRoot 'GakuActors\Repair-Shortcuts.ps1'
    & $shortcut -Desktop:$DesktopShortcuts
} catch {
    for($i=$completed.Count-1;$i -ge 0;$i--){$item=$completed[$i];if($item.Existed){Copy-Item -LiteralPath $item.Backup -Destination $item.Destination -Force}else{Remove-Item -LiteralPath $item.Destination -Force -ErrorAction SilentlyContinue}}
    throw
}
Write-Host "安装完成：$GameRoot"
Write-Host "原有文件备份：$backup"
Write-Host '请按说明安装或更新 Microsoft Visual C++ x64 运行库和 .NET Framework 4.7.2 或更新版本。'
