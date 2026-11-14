# 来源与许可

- `include/GakuPlugin.h`、`include/GakuPaths.h`：复制自本地 `source-clock-fix/include`，基线 Git 提交 `5ecedd0`。为 GakuLoader C ABI v1 消费者头文件，未改动；随附来源仓的 MIT 许可于 `vendor/GakuMotion-LICENSE.txt`。不声称这些文件为本项目原创。
- `vendor/imgui/`：复制自同一基线的 Dear ImGui 源码，保持文件内容及 `LICENSE.txt`；MIT 许可，Copyright (c) 2014-2023 Omar Cornut。
- `vendor/nlohmann/json.hpp`：复制自本地 `source/GakuLocalify-hosted/src/deps/nlohmann/json.hpp`，未改动。其头部保留 Niels Lohmann 的版权和 MIT 许可说明。
- 人物／服装目录读取参考了本地 `gakumas-mod-runtime/manager/src/CampusUiProbe.cpp` 中的 Master 枚举方式和 `GakuLocalify-hosted/src/GakumasLocalify/Hook.cpp` 中的身体、头部关系。新实现独立编写，没有嵌入这些项目的运行时。
- 特殊人物工厂、原生角色管理器和摄影场景方法名称，通过当前安装游戏的离线 IL2CPP metadata 检查。名称和参数数量属于接口线索；不构成可调用性或实机成功证据。

本项目不包含游戏 metadata、人物模型、服装、贴图、头像或其他游戏资源；设计稿使用文字占位与示例数据。

模型实体问题的排查另参考了用户提供的 `https://github.com/chihya72/gakumas-modding` 文档及其链接的 `gakumas-mod-runtime` 项目，核对了实际资产加载、网格／材质／骨骼和初始化的边界。未复制该 GPL 项目的加载／替换源代码；本插件使用独立实现的元数据调用桥接与游戏原生角色加载入口，不将替换现有网格冒充新增完整角色。
