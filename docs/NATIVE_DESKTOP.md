# 独立原生窗口迁移

目标是独立 Windows 进程负责用户界面，游戏内 GakuActors 插件负责在 Unity 主线程读取资源并执行操作。它们通过受限的本地进程通信交换数据；窗口不能接收 Unity 指针，也不能直接调用游戏 API。启动和非拍摄／Live 场景继续休眠。

## Rust 方案调研

- [Microsoft windows-rs](https://github.com/microsoft/windows-rs) 提供 Rust 的 Windows API 绑定，可直接使用系统控件；[native-windows-gui](https://github.com/gabdube/native-windows-gui) 在此类 API 上提供较高层封装。
- [Slint](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/) 支持软件渲染模式，样式更自由，但二进制体积、CPU、刷新和 GPU 效果需要测量，不能仅凭语言判断。
- Windows 桌面仍由系统合成，不能承诺显示窗口时 GPU 使用量为零；[微软文档说明图形架构及 GDI 的 CPU 相关操作](https://learn.microsoft.com/en-us/windows/win32/learnwin32/overview-of-the-windows-graphics-architecture)。游戏本身及三维模型预览仍需渲染。
- 当前选择 Rust 直接调用 Win32 控件做小型原型，暂不引入额外 GUI 框架或 WebView。仅在 UI 更新时重绘，是否低于原窗口需要对照测试。Rust 工具链隔离于工作区 `toolchains/rust-portable`，不修改系统 PATH。

## 当前进度及未完成项

- `DesktopModel.hpp` 将界面快照和命令从 ImGui 拆出；`DesktopProtocol.hpp` 对人物目录／预设／角色、场景节点、道具和控制命令做 JSON 编码及输入校验。游戏插件通过仅由窗口子进程继承的匿名管道通信，命令进入原有主线程队列，未向窗口暴露 Unity 指针或网络端口。
- `desktop-rust` 使用系统控件，提供目录、人物预设与头身组合、加入／定位／移除本插件人物、人物显隐、场景和道具列表、相机纯色及摄影舞台选择；原 ImGui 面板仍保留为回退入口。游戏内对象身份和场景准入仍由原插件验证。
- 人物与预设页已改为三栏工作区，新建和编辑使用身体／头部的图标图库。图标按原生 `GetThumbAssetName` 精确匹配，先读取游戏已加载的 PNG，再尝试通过桌面端读取本地 Octo 缓存并校验资源包 MD5。图片由 CPU 解码后交给 GDI 显示。缺少记录、尚未缓存或匹配不唯一时显示原因。详细流程与验证见 [人物工作区](PEOPLE_WORKSPACE.md)；游戏场景中的界面、保存和加入流程仍待实机验收。
- 用户要求仅通过 CLI 做后续检查之前，本地验证已包含 Rust 单元测试、C++ 启动门控测试、匿名管道 C++/Rust 往返，以及离线模拟人物、节点、PNG 的窗口交互；这些不代表真实游戏内功能或真实性能已经验收。用户后来要求不再进行模拟测试，此后不再执行此类测试。现有 GakuMotion 共存限制仍存在：检测到未协调的动作插件时资源显隐禁用，摄影替换钩子发生冲突时拒绝。
- 卡巴斯基文件监控报告确认早期 Rust 原型被 `VHO:Exploit.Win32.Donut.gen` 启发式规则标记并删除，不能仅凭规则名断言误报。新版 Rust 窗口及插件在安装前使用本机卡巴斯基命令行扫描，报告两个文件均为 OK、0 检出；这不是对今后所有实时拦截情况的保证。未修改杀毒软件配置或添加排除项。
