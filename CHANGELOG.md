# 更新日志

本文件记录 cabinet-status 的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。项目当前只发布浮动的 `latest` Release，没有固定的版本标签，下文的版本号对应 `CMakeLists.txt` 中的 `project(... VERSION 0.1.0)`。

## [未发布]

### 新增

- 同时识别 IIDX 33 的 `IIDX33pc` 与 IIDX 34 的 `IIDX34pc`，共用登录、保存、登出、游客及整机待机释放流程，无需新增配置；新增两代行为回归与无关服务名隔离检查。
- 适配 SOUND VOLTEX `sv7` 正常刷卡流程：以 `sv7_load` 请求的 `refid` 关联完整成功档案响应与最终 `sv7_save`，单曲保存与总结果上报不结束占用，待 `ver07/title.ifs` 源文件打开并完成最终 `imagefs` 挂载后返回空闲。沿用现有状态机、HTTP 接口与 AVS hook 完整性检查。
- SDVX 接入 `soundvoltex.dll` 模块识别与游戏专用事件／资源筛选；新增身份关联、无关响应、保存边界与错误字段的行为测试。使用完整正常流程日志的 137 个 XML 属性树与资源事件回放，验证 143 次实际 HTTP 查询；Windows x64 已交叉编译，实机采集仍待验证。
- 接入 IIDX 游客的 `IIDX33pc.playstart` 请求，不要求卡号或档案 ID；游客与有卡会话均在成功打开并挂载 `sub_demo.ifs` 后释放整机占用。
- IIDX 使用两个固定档案槽位，支持两份档案顺序或交错加载、独立保存／登出及后续会话清理；新增游客、双人、交错响应、资源边界和容量限制的行为回归，保留 SDVX 单会话规则。

### 变更

- 重写 README：按 standard-readme 组织章节，补充安装步骤、配置表、状态流程与已知限制，移除开发期的验收清单和实现细节。
- 新增本更新日志。
- 精简状态机与 IAT 测试中的千次重复读取，保留只读 IAT 页的崩溃回归测试及 PE 导入／导出边界用例；CTest 保持 6 项。

### 修复

- 修复 IIDX 34 首次待机状态未知、下机后占用不释放的问题：`iidx-34-01.txt` 中待机资源为 `/data/graphic//0/sub_demo.ifs`，原先仅识别目录 `1`。现在同时接受目录 `0` 和 `1`，仍要求成功打开及完成 `imagefs` 挂载后释放；增加实际资源匹配驱动的两代状态回归及路径边界检查。6 项 CTest 与现有 HTTP smoke 通过，IIDX 33／34 日志资源及状态事件回放通过 18 次实际 HTTP 查询（档案 ID 为构造值）；Windows x64 DLL 已交叉编译，用户已确认修复版正常可用。
- 修复 IIDX 成功档案响应省略 `fault` 时触发解析故障的问题：保留 `status="0"` 与档案 ID 校验，显式非零 `fault` 不建立占用。
- 区分可恢复的协议／会话错误与采集故障。响应无法解析、登录失败或中断后，可在新的同代待机资源成功打开并挂载时清理会话、恢复空闲；旧代资源、失败的打开／挂载及中途协议事件不能提前恢复。hook 被替换或必要访问器缺失仍保持 503。
- 本次修复通过全部 6 项 CTest、真实 HTTP smoke，以及构造响应驱动的 10 个场景／86 次 HTTP 查询；覆盖缺少 `fault` 的登录与再次登录、错误响应、解析失败后的待机恢复、游客／双人边界和不可恢复的采集故障。Windows x64 DLL 已交叉编译，FireNet 实机验收待执行。
- 修复 IIDX 第二位玩家成功加载不同档案时触发 `unexpected_profile`、随后无法释放占用的问题；单侧保存或登出不释放整机。真正的采集故障仍保持 503，不通过待机事件强制清除。
- 串行化 AVS 属性解码与状态发布，并同步待机资源事件发布，避免并发玩家回调因 generation 变化误报采集故障；原始 AVS 调用仍在该临界区外转发。
- 使用 `iidx-log-07/08/09` 的可见请求／资源时序完成 71 次 HTTP 查询，并以交错响应、混合会话等构造场景完成 30 次 HTTP 查询。日志缺少 IIDX 档案 XML，回放使用构造的有效档案字段，不代表新 DLL 已完成 Windows 游戏实测。

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
