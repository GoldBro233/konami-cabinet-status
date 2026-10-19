# 更新日志

本文件记录 cabinet-status 的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。项目当前只发布浮动的 `latest` Release，没有固定的版本标签，下文的版本号对应 `CMakeLists.txt` 中的 `project(... VERSION 0.1.0)`。

## [未发布]

### 变更

- 重写 README：按 standard-readme 组织章节，补充安装步骤、配置表、状态流程与已知限制，移除开发期的验收清单和实现细节。
- 新增本更新日志。
- 精简状态机与 IAT 测试中的千次重复读取，保留只读 IAT 页的崩溃回归测试及 PE 导入／导出边界用例；CTest 保持 6 项。

## [0.1.0] - 2026-10-19

首个可用版本：IIDX 单人机台占用状态插件，产物为 Windows x64 `cabinet-status.dll` 与 `cabinet-status.ini`。

### 新增

- 通过 spice2x 插件入口挂载，在进程导入地址表上拦截三个 AVS 调用（`property_destroy`、`fs_open`、`fs_mount`），读取参数后原样转发；每个 slot 保存独立前驱，不修改属性树或游戏逻辑。启动时另需 8 个访问器导出用于解析响应，共 11 个。
- 状态机：待机 `free`、登录或结束中 `occupied`，初始化、等待游戏响应、环境不支持或采集失效时 `unavailable`。以完整成功的档案响应作为登录起点，以 `/data/graphic/1/sub_demo.ifs` 打开并挂载为待机终点。
- HTTP 接口 `GET /status`，返回 `{"status":"free"}`、`{"status":"occupied"}` 或 503 `{"error":"state_unavailable"}`；其它路径 404，非 GET 方法 405 并带 `Allow: GET`。响应带 `Cache-Control: no-store`，忽略查询串。
- 基于固定 CivetWeb v1.16 的监听（静态链接，排除 SSL/CGI/文件服务），仅绑定配置的 IPv4 与端口。
- INI 配置 `[Server]` 的 `Enabled`、`Port`、`BindAddress`，只从 DLL 同目录读取一次；文件缺失时使用默认值，取值非法时关闭 HTTP 服务而不影响游戏。
- 游戏识别与 AVS 兼容检查：按 model 识别 IIDX（`LDJ`）与 SOUND VOLTEX（`KFC`），按 AVS core 文件版本匹配 `XCgsqzn/2.17` 导出族，并核对所需导出是否齐全。`2.17.0.0`、`2.17.3.0`、`2.17.4.0` 视为同一族，其它版本不支持；识别为 SOUND VOLTEX 时记录专用适配器尚未实现，接口保持 503。识别不依赖游戏日期、PE 时间戳或入口 RVA。
- 诊断日志：状态 revision／phase／reason、监听地址、模块与 AVS 版本、以及首次解码故障的固定字段；不输出卡号、PIN、档案 ID 或 XML。
- 测试：状态机、配置解析、游戏识别、协议解码、PE64 导入边界、IAT slot 共 6 项 CTest，以及启动真实 listener 的 HTTP smoke（路由、状态切换、并发、端口冲突、半截请求下停止）。
- 交叉编译支持：`linux-debug` 与 `windows-release` 两个 CMake 预设，MinGW-w64 构建的 AMD64 DLL 仅导出 `spice_sdk_entry_point`。
- 随仓库固定 CivetWeb v1.16 与 spice SDK 头文件，附 [third_party/README.md](third_party/README.md) 记录来源与许可。

### 构建与发布

- 新增 `KCS Latest Release` 工作流：推送到 `main` 或手动触发时，在 Ubuntu 24.04 上安装 MinGW-w64 POSIX 工具链，运行 CTest 与 HTTP smoke，交叉编译 Windows x64 DLL，然后创建或更新 `latest` 标签的 Release，并把标签移到本次构建提交。
- 工作流使用固定并发组且不取消进行中的运行，新推送排队等待，避免多次发布争用同一标签。
- Release 附件仅提供 `cabinet-status.dll`、`cabinet-status.ini` 和 `SHA256SUMS`；源码与许可通过说明中指向同一提交的链接提供，早期上传过的源码包、LICENSE 与 `THIRD_PARTY_NOTICES.txt` 会被清理。

[未发布]: https://github.com/GoldBro233/konami-cabinet-status/commits/main
[0.1.0]: https://github.com/GoldBro233/konami-cabinet-status/releases/tag/latest
