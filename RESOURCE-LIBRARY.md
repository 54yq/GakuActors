# 第一阶段资源库入口

桌面窗口顶部的“资源库”切换到内置资源库；使用“游戏管理”切回原有界面。搜索、选择、依赖计划、导出与任务都在桌面窗口内进行，下载与转换在游戏进程外执行。默认后端为桌面 EXE 旁的 `ResourceBackend`。发行包使用程序目录中的 `runtime`，开发环境可使用后端的 `.venv`；后端工具放在 `ResourceBackend/tools`。`Open-ResourceLibrary.ps1` 也须与 EXE 放在一起。`resource-backend.txt` 可指定相对桌面目录的路径；环境变量 `GAKUACTORS_RESOURCE_BACKEND` 优先。

发布候选源码中，后端项目位于 `resource-backend/`，其源码与许可证随主项目一同提供；本机独立后端工作目录仍可继续单独使用。

数据默认位于 `%LOCALAPPDATA%/GakuActors/ResourceLibrary`，可用 `GAKUACTORS_RESOURCE_DATA` 指定。每次启动桌面程序及打开资源库时后台检查 PC 在线清单；有新修订时自动导入，网络失败时继续使用已保存的清单。任务进行中跳过更新。也可手动导入独立下载器保存的清单副本；不会搬移既有数据。检查结果保存在数据目录 `catalog-update-status.txt`。服务使用 127.0.0.1:8777，保持运行以便任务在窗口关闭后继续；再次打开会验证进程及数据目录并复用。不关闭其他服务，端口被占用时报告错误。后台日志位于数据目录。需要停止时先确认任务已结束，再按数据目录 service.json 的 PID 核实对应 app.py 进程并停止。

资源 ID、清单 fingerprint、文件 hash 与游戏模型和场景实例属于不同身份，当前没有验证过的映射；下载或导出不代表游戏加载成功，不能从资源库直接加载人物。保留原有游戏桥接及乐库。导出范围、限制和许可证见独立项目 README、LICENSE、THIRD_PARTY_NOTICES.md。独立下载器保持原有 8776 与 data 目录，可单独回归。

标准安装位置为游戏根目录下的 `GakuActors` 文件夹。程序旁的 `game-root.txt` 内容为 `..`，相对于程序目录解析到游戏根目录，以启用游戏自动连接和乐库；环境变量 `GAKUACTORS_GAME_ROOT` 优先，可指定路径。

在线清单协议参考 [GkmasObjectManager](https://github.com/AllenHeartcore/GkmasObjectManager)（GPL-3.0，作者 AllenHeartcore，相关来源见该项目说明）。接口使用固定 PC 目录版本 705100；若服务器协议或该版本号变化，自动检查可能失败，手动导入仍可用。
