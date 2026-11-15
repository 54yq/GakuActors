# 来源与许可

- 乐库清单解码和资源头掩码格式参考 [gkmasToolkit](https://github.com/kishidanatsumi/gkmasToolkit) 的 `ManifestDecryptor.py`、`octodb.proto` 和 `MaskedHeaderStream.py`。Live 音频导出使用 [UnityPy](https://github.com/K0lb3/UnityPy) 及其依赖；安装目录中的 `GakuActors.Desktop.deps` 保留各发行包的许可与元数据。通用音频解码使用 [vgmstream](https://github.com/vgmstream/vgmstream)；安装目录 `GakuActors.Vgmstream` 保留发布包的许可证和随附文件。

- `include/GakuPlugin.h`、`include/GakuPaths.h`：复制自本地 `source-clock-fix/include`，基线 Git 提交 `5ecedd0`。为 GakuLoader C ABI v1 消费者头文件，未改动；随附来源仓的 MIT 许可于 `vendor/GakuMotion-LICENSE.txt`。不声称这些文件为本项目原创。
- `vendor/imgui/`：复制自同一基线的 Dear ImGui 源码，保持文件内容及 `LICENSE.txt`；MIT 许可，Copyright (c) 2014-2023 Omar Cornut。
- `vendor/nlohmann/json.hpp`：复制自本地 `source/GakuLocalify-hosted/src/deps/nlohmann/json.hpp`，未改动。其头部保留 Niels Lohmann 的版权和 MIT 许可说明。
- `camera-core/` 基于本地 GakuLocalify hosted 源码及其相机、输入屏蔽实现，基线提交 `cd5db7bda8009bdd52aed43309c7b827f681fbb3`，包含当地后续修改。该代码遵循 GPL-3.0，文本见 `camera-core/LICENSE`；已与主插件链接的整合版 DLL 按 GPL-3.0 发行，不作为 MIT 代码重新授权。构建保留了部分未启用的 Localify 源码以满足编译依赖；运行时不启用其汉化界面和翻译钩子。
- `vendor/localify-camera/` 是早期整理的相机源文件快照，不参与构建。
- 人物／服装目录读取参考了本地 `gakumas-mod-runtime/manager/src/CampusUiProbe.cpp` 中的 Master 枚举方式和 `GakuLocalify-hosted/src/GakumasLocalify/Hook.cpp` 中的身体、头部关系。新实现独立编写，没有嵌入这些项目的运行时。
- 特殊人物工厂、原生角色管理器和摄影场景方法名称，通过当前安装游戏的离线 IL2CPP metadata 检查。名称和参数数量属于接口线索；不构成可调用性或实机成功证据。

本项目不包含游戏 metadata、人物模型、服装、贴图、头像或其他游戏资源；设计稿使用文字占位与示例数据。

- `src/NativeResources.inl` 中的道具名称保护规则改编自 GakuMotion 的 `src/GkLiveActorPropPolicy.hpp`，场景视觉节点名称分类参考 `src/GkStageManager.hpp`，基线提交 `1757f8e`。其 MIT 许可及版权声明保存在 `vendor/GakuMotion-LICENSE.txt`。
- 摄影背景载入钩子的接口定位和预设名称参考同一提交中的 `src/GkStageLoadOverride.hpp`、`src/GkStageLoadPolicy.hpp`；纯色相机操作及场景保护名单参考 `src/GkStageManager.hpp` 和 `src/GkStageRuntime.hpp`。本项目的实现及生命周期保护仍需单独验证。
- Rust 窗口使用的运行时依赖由 `desktop-rust/Cargo.lock` 锁定；所选 MIT 许可文本和各库版权声明保存在 `desktop-rust/licenses/`。窗口程序发布时应一并提供这些许可文件。

模型实体问题的排查另参考了用户提供的 `https://github.com/chihya72/gakumas-modding` 文档及其链接的 `gakumas-mod-runtime` 项目，核对了实际资产加载、网格／材质／骨骼和初始化的边界。未复制该 GPL 项目的加载／替换源代码；本插件使用独立实现的元数据调用桥接与游戏原生角色加载入口，不将替换现有网格冒充新增完整角色。

`compat/gakumotion-visual-handoff.patch` 针对 GakuMotion 基线 `1757f8e` 增加资源控制交接服务；补丁涉及的 GakuMotion 源码继续遵循其 MIT 许可，版权和许可文本见 `vendor/GakuMotion-LICENSE.txt`。
