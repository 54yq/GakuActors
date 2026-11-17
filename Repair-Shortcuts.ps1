# Run from the installed GakuActors directory after moving the game.
[CmdletBinding()]
param([switch]$Desktop)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'GakuActors.Desktop.exe'
if(!(Test-Path -LiteralPath $exe -PathType Leaf)){throw '请在已安装的 GakuActors 文件夹中运行此脚本。'}
$shell=New-Object -ComObject WScript.Shell
$items=@(
    @{Name='GakuActors 场景资源管理.lnk'; Arguments=''; Icon='GakuActors.ico'},
    @{Name='GakuActors 乐库.lnk'; Arguments='--music-only'; Icon='GakuActors-Music.ico'}
)
foreach($item in $items){
    $path=Join-Path $PSScriptRoot $item.Name
    $link=$shell.CreateShortcut($path)
    $link.TargetPath=$exe
    $link.Arguments=$item.Arguments
    $link.WorkingDirectory=$PSScriptRoot
    $icon=Join-Path $PSScriptRoot $item.Icon
    if(Test-Path -LiteralPath $icon -PathType Leaf){$link.IconLocation=$icon+',0'}
    $link.Save()
    if($Desktop){Copy-Item -LiteralPath $path -Destination ([Environment]::GetFolderPath('DesktopDirectory')) -Force}
}
