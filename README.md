# GakuActors · 桌面资源库与游戏内场景管理

GakuActors 用于浏览、下载和导出《学园偶像大师》的资源，也可通过 GakuLoader 插件管理游戏内的人物、舞台、道具和相机。打开桌面客户端即可使用资源库。连接游戏后，切到 **游戏管理** 操作当前场景。

[下载 Windows x64 测试安装包](https://github.com/54yq/GakuActors/releases/download/v0.5.1-test.1/GakuActors-v0.5.1-test.1-win-x64.zip) · [发布说明与源码包](https://github.com/54yq/GakuActors/releases/tag/v0.5.1-test.1) · [安装与升级说明](release/安装说明.md)

## 你可以用它做什么

| 入口 | 功能 | 所需环境 |
| --- | --- | --- |
| **资源库** | 按中文标签和原始名称检索，查看依赖和下载大小，导出受支持的 FBX、PNG 和 Unity `.anim`。 | Windows 客户端及随包后端，可独立于游戏运行。 |
| **乐库** | 搜索、试听、拖动进度及导出游戏音乐为 WAV。 | 本地游戏目录及音频清单，游戏可以关闭。 |
| **人物与预设** | 浏览服装和部件，组合身体、脸部与发型，保存预设并将新人物加入场景。 | 首次读取人物目录和新增人物需连接游戏。 |
| **场景人物、场景管理、道具管理、舞台导入** | 调整站位和朝向、控制资源显隐、选择纯色背景及下次载入的摄影舞台。 | 需要 GakuLoader 和支持的拍摄或 Live 场景。 |
| **自由视角** | 在自由、跟随和第一人称镜头之间切换。 | 需要连接游戏。 |

## 安装与启动

需要 Windows x64。安装包附带 Python 运行时、HCA 解码器和导出组件。系统需要 Visual C++ x64 运行库与 .NET Framework 4.7.2 或更新版本。

- **只用资源库**。把解压后的 `payload/GakuActors/` 整个文件夹放在可写目录，打开 `GakuActors.Desktop.exe`，点击顶部 **资源库**。初次使用会尝试检查在线清单，也可以导入已有清单。缓存清单可离线检索。
- **安装到游戏或升级**。关闭游戏和客户端，运行 `Install.cmd`，选择包含 `gakumas.exe` 的目录。安装器保留已安装的 GakuLoader，并备份即将覆盖的文件。若曾使用分离版 `GakuActorsCameraCore.dll`，先停用它，避免重复注册。
- **启动游戏管理**。运行游戏，再用游戏目录内的场景管理快捷方式或 `GakuActors/GakuActors.Desktop.exe` 启动客户端。客户端显示已连接后选择左侧页面。若未连接，使用 **手动连接**。GakuLoader 面板也保留 **模型界面**和 **Live 入场前换装**入口。

```text
<游戏根目录>/
├── gakumas.exe
├── version.dll
├── GakuLoader/
│   ├── plugins/GakuActors.dll
│   └── Configs/gakuactors/
└── GakuActors/
    ├── GakuActors.Desktop.exe
    ├── game-root.txt
    ├── ResourceBackend/
    ├── GakuActors.Vgmstream/
    └── runtime/
```

`game-root.txt` 默认为 `..`。移动游戏目录后，在新位置运行 `./Repair-Shortcuts.ps1 -Desktop` 重建快捷方式。自定义预设位于 `GakuLoader/Configs/gakuactors/`，资源库数据默认位于 `%LOCALAPPDATA%/GakuActors/ResourceLibrary`。

## 桌面资源库和乐库

**资源库**支持将中文标签与原始名称混合搜索，例如 `花海咲季 发型 cstm`，也可以叠加类别筛选。人物、服装和歌曲名称取决于本机可用的资料表。已有清单会自动补标，无法分类的条目显示 **待识别**。选中资源后可查看依赖，按需下载并导出。导出的文件用于外部工具，场景中的人物通过下文的游戏管理入口添加。[资源库操作与数据位置](RESOURCE-LIBRARY.md)

**乐库**可从客户端或独立快捷方式打开。选择曲目和版本后可试听、暂停、拖动或导出 WAV。未缓存条目会按清单下载并校验到插件缓存，试听音量独立保存。当前通用音频支持单 HCA 音轨 AWB，Live 音频支持受支持的单音频 Unity 资源包。

## 游戏内工作流

### 加入人物并绑定动作

1. 进入支持的拍摄或 Live 场景，打开 **人物与预设**。如果窗口仍显示缓存目录，到 **资源设置**里选择 **读取人物目录**。
2. 选中人物和造型，查看身体与头部预览。需要自己的搭配时，点 **新建预设**，选择头身组合或单独选择身体、脸部和头发，然后保存。保存过的预设也可在场景外编辑。
3. 停止 Motion 播放，在 **资源设置**中启用人物加载。选一个场上参考人物，再点击 **加入人物**。
4. 新人物初始出现在参考人物的位置。打开 **场景人物**，选中标有 **本插件新增** 的人物，调整 X / Y / Z 和朝向。也可在此移除经确认由插件创建的人物。
5. 需要外部动作时，在 **GakuMotion** 刷新场上人物，选择对应人物并绑定动作。自定义预设名可由支持该接口的 Motion 版本读取。[GakuMotion 分支](https://github.com/54yq/GakuMotion)

现有人物模型热替换暂时停用。Live 入场前换装面板目前提供只读识别与预设浏览。可用角色、部件及支持场景取决于游戏版本与已下载资源。

### 道具、舞台和相机

- 打开 **道具管理 → 扫描场景资源**。0.5.1 会扫描角色模型子层级，标出麦克风、行李箱、荧光棒、水枪和糖果等道具。其他符合命名规则的条目会显示原始节点名。行李箱与对应网格分别记录原有显隐，恢复时按各自状态还原。
- **场景管理**提供可编辑资源的显隐和纯色背景。**舞台导入**可将受支持的舞台应用到下次摄影背景载入。Live 暂停操作会同时暂停舞台和音乐。
- 与支持 `gakumotion.visual-handoff` 的 GakuMotion 配合时，使用 **启用资源控制** 请求接管，操作结束后选择 **交还 Motion 资源控制**。断开使用过资源控制的窗口时会尝试恢复。摄影背景钩子由加载器独占，可能受其他插件影响。
- 自由视角用 **W/A/S/D** 移动、**空格/Ctrl** 升降、**方向键**转向、**Q/E** 调视野、**R** 重置、**F** 换模式、**V** 换子模式。支持交接的 Motion 可在关闭自由视角后接回镜头。

## 排查与反馈

- **目录为空或按钮灰色**。等待游戏进入受支持的场景，连接窗口后刷新目录或扫描。加入人物还需要启用人物加载、选择预设及参考人物。
- **预览图片缺失**。可先在游戏中打开相应服装页，再点击 **重试当前图片**。客户端会报告未缓存、匹配不唯一或解析失败等原因。
- **找不到新增人物**。先在 **场景人物**里定位本插件新增的人物并调整站位，再检查是否构建失败。
- **报告问题**。提供游戏、GakuLoader、GakuActors 及 GakuMotion 版本、场景、复现步骤和 `GakuLoader/Logs/gakuloader.log` 中相关的 `[GakuActors]` 日志。[问题反馈](https://github.com/54yq/GakuActors/issues) · [Live 诊断与日志](docs/LIVE_DIAGNOSTICS.md)

## 构建、来源与许可

源码构建需 Windows x64、MSVC、Windows SDK、CMake 4、Conan、Rust 和 Cargo。按 `camera-core/hosted/conanfile.py` 准备 Conan toolchain 后，从仓库根目录执行：

```powershell
./build.ps1 -Target Runtime -ConanToolchain <ConanToolchain路径>
./build-desktop.ps1
```

[Windows 发布构建说明](release/构建说明.md) · [Slint 客户端](docs/SLINT_DESKTOP.md) · [验证记录](docs/VALIDATION.md)

[GakuLoader](https://github.com/Gaku-Mod-Tools/GakuLoader) 提供插件宿主。部分道具保护、场景规则和摄影背景逻辑参考 [crested-ezios/GakuMotion](https://github.com/crested-ezios/GakuMotion)。本项目同时参考 [gakumas-modding](https://github.com/chihya72/gakumas-modding) 与 [gakumas-mod-runtime](https://git.chinosk6.cn/chihya72/gakumas-mod-runtime)。

原有项目代码采用 [MIT](LICENSE)。整合相机的插件及包含 Slint 的桌面版随 [GPL-3.0](camera-core/LICENSE) 组件分发。其他依赖分别遵循其原许可证。游戏及资源权利属于相应权利人。[第三方来源与许可证](THIRD_PARTY_NOTICES.md)
