#Requires -Version 5.1
[CmdletBinding(DefaultParameterSetName='Cargo')]
param(
    [Parameter(ParameterSetName='Cargo')][string]$Cargo = 'cargo',
    [Parameter(ParameterSetName='Cargo')][switch]$Offline,
    [Parameter(ParameterSetName='Executable', Mandatory)][string]$TestExe,
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (!$OutputRoot) { $OutputRoot = Join-Path $repo 'build/desktop-visual-review' }
$root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
$run = Join-Path $root (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $run | Out-Null

$filter = 'desktop_visual_checks::render_native_review_matrix'
$previous = $env:GAKUACTORS_VISUAL_OUTPUT
try {
    $env:GAKUACTORS_VISUAL_OUTPUT = $run
    if ($PSCmdlet.ParameterSetName -eq 'Executable') {
        $testBinary = (Resolve-Path -LiteralPath $TestExe).Path
        & $testBinary --exact $filter --nocapture
    } else {
        $cargoArgs = @('test','--locked','--manifest-path',(Join-Path $repo 'desktop-rust/Cargo.toml'),'--bin','gakuactors-desktop')
        if ($Offline) { $cargoArgs += '--offline' }
        & $Cargo @cargoArgs -- --exact $filter --nocapture
    }
    if ($LASTEXITCODE -ne 0) { throw '原生渲染检查失败。' }
} finally {
    $env:GAKUACTORS_VISUAL_OUTPUT = $previous
}

function ConvertTo-HtmlText([string]$Text) { [Net.WebUtility]::HtmlEncode($Text) }
function Get-FileUri([string]$Path) { ([Uri](Get-Item -LiteralPath $Path).FullName).AbsoluteUri }
$pages = @(
    @{ Key='home-offline'; Title='人物与预设 · 默认首页离线'; Ref='actors-night-home-offline.png'; Check='两栏起始位置、紧凑按钮、小面积空状态和导航选中条。'; Sizes=@('desktop','dpi150','compact') },
    @{ Key='home-populated'; Title='人物与预设 · 有造型与 Live 数据'; Ref='actors-night-home-populated.png'; Check='目录与卡片的层级、有效预设才出现的详情、入场控制位置。'; Sizes=@('desktop','dpi150','compact') },
    @{ Key='scene-actors'; Title='场景人物'; Ref='actors-night-theme.png'; Check='人物列表在站位图上方，右侧显示位置、朝向和显隐控制。'; Sizes=@('desktop','dpi150','compact') },
    @{ Key='scene-props'; Title='场景与道具'; Ref='actors-night-props.png'; Check='统一扫描、资源控制条、两组资产和右侧详情，标签与显隐列对齐。'; Sizes=@('desktop','dpi150','compact') },
    @{ Key='home-startup-empty'; Title='人物与预设 · 初次运行'; Check='没有缓存与人物数据时仍提供读取目录入口，预设操作正确禁用。'; Sizes=@('desktop') },
    @{ Key='preset-edit'; Title='预设编辑'; Check='同一套页头、选项卡、表单和面板，保存入口可辨。'; Sizes=@('desktop') },
    @{ Key='scene-props-empty'; Title='场景与道具 · 离线空列表'; Check='两组搜索与空状态都完整显示。'; Sizes=@('desktop') },
    @{ Key='scene-props-readonly'; Title='场景与道具 · Live 前只读'; Check='可手动扫描，显隐和接管控件正确禁用。'; Sizes=@('desktop') },
    @{ Key='stage-import'; Title='舞台导入'; Check='导入说明和操作靠上，离开摄影模式时有明确状态。'; Sizes=@('desktop') },
    @{ Key='resource-library'; Title='资源库与导出'; Check='共用导航和标题体系，保留搜索、选择和导出入口。'; Sizes=@('desktop') },
    @{ Key='music'; Title='独立乐库'; Check='共用暗夜颜色与文本层级，核对搜索、结果、详情和播放栏。'; Sizes=@('wide','compact-dpi150') }
)
$sections = foreach ($page in $pages) {
    $cards = @()
    if ($page.Ref) {
        $ref = Join-Path $repo ('preview/' + $page.Ref)
        if (!(Test-Path -LiteralPath $ref)) { throw "缺少设计基准图 $ref" }
        $cards += "<figure><figcaption>设计基准</figcaption><a href='$(Get-FileUri $ref)'><img src='$(Get-FileUri $ref)' alt='设计基准'></a></figure>"
    }
    foreach ($size in $page.Sizes) {
        $file = Join-Path $run "$($page.Key)-$size.png"
        if (!(Test-Path -LiteralPath $file)) { throw "缺少原生截图 $file" }
        $label = switch ($size) {
            desktop {'1440×940 · 100%'}
            dpi150 {'1320×990 · 150%'}
            compact {'880×440 · 100%'}
            wide {'1280×800 · 100%'}
            'compact-dpi150' {'1040×660 · 150%'}
        }
        $uri = Get-FileUri $file
        $cards += "<figure><figcaption>Slint 原生渲染 · $label</figcaption><a href='$uri'><img src='$uri' alt='Slint 原生渲染 $label'></a></figure>"
    }
    "<section><h2>$(ConvertTo-HtmlText $page.Title)</h2><p>$(ConvertTo-HtmlText $page.Check)</p><div class='grid'>$($cards -join '')</div></section>"
}
$pageText = @"
<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><title>GakuActors 桌面视觉检查</title>
<style>
:root{color-scheme:dark}body{margin:24px auto;max-width:1500px;padding:0 20px;background:#101219;color:#eef1f7;font:14px Segoe UI,Microsoft YaHei,sans-serif}section{border-top:1px solid #2b3243;margin-top:24px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}figure{margin:0;background:#1a1e29;border:1px solid #2b3243;border-radius:8px;overflow:hidden}figcaption{padding:8px 10px}img{display:block;width:100%;height:auto}p{color:#a7b0c5}@media(max-width:780px){.grid{grid-template-columns:1fr}}
</style>
<h1>GakuActors 桌面视觉检查</h1>
<p>固定示例数据。对照同一状态的设计基准和原生渲染，打开缩略图可看原尺寸。</p>
$($sections -join [Environment]::NewLine)
</html>
"@
$report = Join-Path $run 'index.html'
$pageText | Set-Content -LiteralPath $report -Encoding utf8
Write-Output $report
