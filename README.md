# cabinet-status

机台单人占用状态插件，产物为 Windows x64 `cabinet-status.dll` 与同目录 `cabinet-status.ini`。Linux 开发、MinGW-w64 交叉编译，HTTP 使用固定 CivetWeb v1.16。

## 运行要求与游戏识别

运行要求是 Windows x64、可用的 Spice SDK 及下表所列的兼容 AVS 接口。插件通过 SDK 的 model 自动识别游戏，LDJ 不作为整个插件的统一前提；不限制游戏日期、PE 时间戳或入口 RVA，也不比较 spice2x 的发布版本字符串。这些信息只用于诊断和记录实测环境。

| 自动识别的游戏 | model | 当前状态采集 |
| --- | --- | --- |
| beatmania IIDX | `LDJ` | 已实现 `IIDX33pc` 协议的单人采集，不按日期或 PE 构建标识拒绝启动 |
| SOUND VOLTEX | `KFC` | 已实现游戏识别及 AVS 匹配；专用会话采集尚未实现，接口保持 503 |
| 其它游戏 | 其它 model | 记录无法识别的 model，接口保持 503 |

识别游戏和实现该游戏的占用判定是两个独立步骤。当前 IIDX 采集仍读取 `IIDX33pc` 及对应资源边界；取消构建白名单不代表其它代数或其它协议已经完成适配。单人表示同一时刻最多一位刷卡登录玩家，状态逻辑不使用 P1/P2 编号；游客、新卡注册和双人并发不在当前范围，P2 的实机路径仍待单独验收。

## AVS 兼容范围

| AVS core 文件版本 | 接口族 | 匹配依据 |
| --- | --- | --- |
| `2.17.0.0` | `XCgsqzn/2.17` | 固定 spice2x 上游导出表 |
| `2.17.3.0` | `XCgsqzn/2.17` | 固定 spice2x 上游导出表，所需函数与上项相同 |
| `2.17.4.0` | `XCgsqzn/2.17` | 本机 Windows 实测 |

AVS 文件版本必须能匹配已知 ABI，且所需的 11 个 named exports 必须全部存在；随后还要确认调用方 IAT 覆盖。未知 AVS 版本、缺少导出、缺少所需 SDK 接口或没有游戏采集适配器时，HTTP 保持 503 并记录具体原因，不猜测其它版本的 ABI。上表的导出表匹配不等于该环境的完整游戏流程已经实测。

## 本机实测环境

以下为本机已完成 Windows 游戏流程的环境记录，**不是游戏日期、硬件型号、spice2x 发布版本或 PE 构建白名单**：

| 项目 | 本机实测值 |
| --- | --- |
| 操作系统 | Windows x64，OS build `26300` |
| 游戏 | beatmania IIDX 33 Sparkle Shower，日期 `2026081900` |
| 游戏标识 | `LDJ:J:D:X:2026081900`，实测组合中 Omnifix 使用 X revision |
| 游戏模块 | `bm2dx.dll`，AMD64，时间戳 `0x6a713632`，入口 RVA `0x00b1cccc` |
| spice2x | `1.0-V-2026-09-12T22:19:45`，x64 |
| AVS core | `2.17.4.0`，`XCgsqzn` 导出族 |
| 同时加载的插件 | `ifs_hook.dll`（layeredFS 4.1）、`omnifix.dll`（0.7.2.4）、`2dx-gsm.dll` |
| CPU / GPU | AMD Ryzen 9 7945HX / NVIDIA GeForce RTX 4060 Laptop GPU |
| HTTP 监听 | 实测配置为 `0.0.0.0:18080`；发行默认值仍为 `127.0.0.1:18080` |

实测时启用了 `-automap`、`-netdump`、`-loglevel all`、`-avsverbose`、`-ea3verbose` 和 `-graphicsverbose`。它们是采样参数，不是插件的运行要求；关闭这些开关后的对照验收尚未完成。SDVX 目前没有占用状态实测结论。

## 验证状态

| 范围 | 已完成的验证 |
| --- | --- |
| 平台无关逻辑 | 6 项 CTest：状态机、配置、游戏识别与 AVS 兼容边界、AVS 解码、PE64 导入边界、只读 IAT 页面读取 |
| HTTP | Linux 真实 CivetWeb listener：路由、状态切换、并发、端口冲突、停止监听 |
| Windows 构建 | MinGW-w64 交叉编译，AMD64 DLL，仅导出 `spice_sdk_entry_point`，静态链接 CivetWeb 和 C++／线程运行库 |
| Windows 登录退出 | 成功加载档案后 occupied，logout 后保持占用，待机挂载后 free |
| Windows 付费游玩 | 实际扣费、单曲成绩提交、最终 save 期间持续占用，待机挂载后 free，正常关闭 |

实机记录来自私有的 `iidx-log-05` 和 `iidx-log-06`，证明了上述本机环境及插件组合下这两条正常路径。原始日志不提交或发行，也不作为测试快照。记录未附 HTTP 轮询，不能据此断言端到端采样延迟已通过；新增的识别／AVS 匹配分支、本机环境以外的版本、扩展配置、异常路径、P2、连续再次登录及关闭采样开关后的运行仍需各自验收。

## 安装和查询

1. 将 `out/windows-x64/cabinet-status.dll` 与 `cabinet-status.ini` 一起复制到现有 Windows 插件目录。
2. 保留原 spice 参数，在现有普通 `-k` 的 DLL 列表末尾追加 `cabinet-status.dll`，排在 `ifs_hook.dll omnifix.dll 2dx-gsm.dll` 之后。不需要 `-z`。
3. 启动游戏，检查 `cabinet-status` 日志中的实际 game/model、日期、PE 标识、AVS 版本及 ABI、caller 的 destroy/open/mount slot 数量和监听地址。SDVX 当前会明确记录专用状态适配器尚未实现。
4. 用 `curl.exe -i http://127.0.0.1:18080/status` 查询。IIDX 初始化时返回 503，成功观察到待机资源边界后返回 free；已识别但尚无采集适配器的游戏保持 503。

默认配置：

```ini
[Server]
Enabled = 1
Port = 18080
BindAddress = 127.0.0.1
```

仅启动时读取 DLL 旁的 INI，与工作目录无关。缺失文件采用上述默认值并告警；读取失败、超过 64 KiB、已知 key 重复或值非法时，本次插件服务禁用，不终止游戏。`Enabled=0` 不安装 hook、不监听。Port 为十进制 1..65535，BindAddress 仅接受四段十进制 IPv4。支持 UTF-8 BOM、CRLF/LF、大小写不敏感的 section/key 和行首 `;` / `#` 注释。未知 key/section 忽略并集中告警一次。不会热重载、换端口或自动重试。

| 请求或状态 | HTTP | 精确 JSON body |
| --- | --- | --- |
| GET /status，待机 | 200 | `{"status":"free"}` |
| GET /status，登录或结束中 | 200 | `{"status":"occupied"}` |
| GET /status，初始化／等待档案响应／不支持／捕获失效 | 503 | `{"error":"state_unavailable"}` |
| 其它路径 | 404 | `{"error":"not_found"}` |
| /status 的其它 method | 405 | `{"error":"method_not_allowed"}` |

允许 `/status?query`，不解释查询串。响应均有 JSON Content-Type、`Cache-Control: no-store` 和 `Connection: close`；正常响应有精确 Content-Length。HEAD 按路径返回 404 或 405，省略 body 与 Content-Length；405 包含 `Allow: GET`。无状态控制 HTTP 接口、无 CORS 通配符。

## 当前 IIDX 状态依据和边界

- 只观察 AVS 静态导入调用，不依赖 `-netdump`、`-automap` 或 verbose 日志，不修改属性树或游戏逻辑。
- 唯一未完成 get 加上完整、成功、自描述的 profile 响应后进入 occupied。刷卡／PIN 等待、PASELI 开关和裸成功响应不代表登录／退出。
- 尚无活动档案而 get 正在进行时返回 503；完整成功响应后才 occupied。空 property 在根查询前后均无 AVS 错误时忽略；根查询引入错误、目标字段缺失或类型错误仍按故障处理。
- 匹配当前内存中档案的 logout/save 请求进入 ending，HTTP 仍是 occupied。
- `/data/graphic/1/sub_demo.ifs` 成功 open，随后同 generation 的 imagefs mount 返回正数，才回到 free；接受重复 `/` 和动态 `/afp<数字>` 前缀。
- 这个资源边界是对限定版本与已有样本的工程假设，未证明预加载、缓存和当前 mod 组合下恒成立。若演奏中预加载或返回待机绕过它，应禁用可用状态，取得真实场景入口证据后修正。
- 重叠 get、未完成 get 跨待机、无来源／冲突 profile、关键字段解码失败、IAT 被替换会进入本进程不可恢复的 503；重启游戏恢复观察。没有超时释放。
- IAT 每个 slot 保存独立前驱，原参数和返回值透传。监测 slot 每 500ms 进行；发现其它包装替换时停止观察，不抢写。
- IAT 归属检查采用纯原子 load，可读取只读页面；只有实际条件替换 slot 时临时解除页面写保护。每类 API 最多 64 个 wrapper，调用方模块及本插件固定到进程结束。
- SDK destroy 先发布不可用，再令管理线程停止 HTTP；wrapper 与前驱记录保留到进程结束，后续 AVS 调用只转发。不支持运行中 FreeLibrary 热卸载。进程强杀不能保证完成清理。

状态和诊断不输出卡号、PIN、档案 ID 或 XML。档案 ID 只在内存固定缓冲中用于单人归属检查。状态变化诊断允许合并快速连续 revision，输出 revision、phase、reason。首个解码故障另输出一次 `first_decode_fault`，包含固定 stage、issue、节点类型、accessor 返回码、AVS error 和 network_root 标志。若 AVS error 仅在解码结束时被发现，stage 表示最后检查阶段，不保证是首次引入错误的位置。wrapper 内不调用 SDK 日志或写磁盘。

## 构建和本地验证

需要 CMake 3.25+、Ninja、本地 C/C++ 编译器、Python3、MinGW-w64 x64 和 LLVM 工具。无需 Wine 或微软 SDK。C 为 C11，C++ 为 C++20；依赖源码已经随工程固定保存，见 [third_party/README.md](third_party/README.md)。

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
python3 tests/http_smoke.py --server build/linux-debug/http-smoke-host
cmake --preset windows-release
cmake --build --preset windows-release
cmake --install build/windows-release --component Runtime --prefix out/windows-x64
llvm-readobj --file-headers --coff-exports --coff-imports out/windows-x64/cabinet-status.dll
sha256sum out/windows-x64/cabinet-status.dll out/windows-x64/cabinet-status.ini
```

`Runtime` 只安装 DLL 和 INI。Linux CTest 包括状态轨迹、配置、游戏识别与 AVS ABI 选择、AVS accessor 合成树解码、PE64 导入边界和 IAT 只读页面读取；所有树和身份均人工合成，不使用原始日志快照。HTTP smoke 启动同一 HttpServer 的真实 CivetWeb listener，检查 free→occupied→ending→free、503/404/405、HEAD、原始路径、查询串、缓存和长度、并发读取、端口冲突及半截请求下停止监听。测试 host 的 stdin 命令不编入发布 DLL。

Linux 检查验证可移植逻辑与 HTTP，Windows 日志验证已观察到的游戏路径。WinAPI 排他监听、不同配置、其它退出条件及未覆盖的共存场景，仍需分别在 Windows 验收。构建目录和 `out/` 均为生成文件，不纳入版本控制。

## GitHub Actions 自动构建与发布

每次推送到 `main` 都会运行 `KCS Latest Release` 工作流，也支持从 Actions 页面手动运行。GitHub-hosted Ubuntu 24.04 runner 安装 MinGW-w64 POSIX 工具链，运行现有 Linux 行为测试和 HTTP smoke，然后使用 `windows-release` 预置交叉编译 Windows x64 DLL。

构建成功后，工作流创建或更新固定标签 `latest` 的 Release，标题固定为 **KCS Latest Release**。Release 附件仅提供 `cabinet-status.dll`、`cabinet-status.ini` 和 SHA256SUMS；源码及许可通过 Release 说明中的对应提交链接提供，不上传 `cabinet-status-source.tar.gz`、LICENSE 或 THIRD_PARTY_NOTICES.txt，并自动清理已有的这三个同名附件。运行插件只需 DLL 和 INI；更新现有安装时保留自己的 INI 配置。`latest` 标签会移动到这次成功发布的构建提交，说明中的源码链接也固定到同一提交。

工作流使用固定 concurrency group、`cancel-in-progress: false` 和 `queue: max`：新推送等待当前运行完成，最多保留 100 个等待中的运行，不以新推送替换旧的待运行构建。GitHub 按进入并发等待队列的先后处理运行，未承诺不同派发事件的严格全局排序。

可通过 `gh run list --workflow release.yml --branch main` 查看运行，通过 `gh release view latest` 查看 Release。GitHub 的源码下载由对应提交生成，不包含本地忽略的证据日志或构建目录。


## Windows IIDX 适配器验收

先核对初始化日志：游戏识别为 IIDX，AVS 版本匹配已知 ABI，三类 API 均有 slot，property_destroy caller 至少包含 bm2dx 或 avs2-ea3，监听成功。缺少必要 slot、无法读取 AVS 版本／导出或检测到 `iat_replaced` 时保持 503，保留 caller/API 诊断。静态 IAT 不能覆盖 AVS 内部直接调用或 GetProcAddress 动态调用，必须通过场景对照证明目标事件确实可见。

另开 PowerShell 观察（Ctrl+C 停止）：

```powershell
while ($true) {
    Get-Date -Format o
    curl.exe -s -i --max-time 1 http://127.0.0.1:18080/status
    Start-Sleep -Milliseconds 200
}
```

按以下顺序记录画面阶段、接口和插件诊断；目标为观察到 profile 或 idle 边界后 1 秒内反映状态，包含消费者采样时间。

1. 启动→待机：503→200/free，待机期间不误占用。
2. 正确 PIN→资料→支付前停留至少 10 秒→退出→待机：资料后 occupied，未付款仍占用，直到待机才 free。
3. 刷卡→PIN 等待→取消：全程 free。再验证错误 PIN 后取消，以及错误 PIN 后改为正确 PIN。
4. 付费→选曲→完整一曲→结算→退出→上传→待机：occupied 连续不中断。随后同档案再登录一次，最后从 P2 完成一次单人登录／退出。
5. 仅在已知安全或自然出现的失败中验证：上传失败但 idle 到达可 free；未完成 get 被中断应 503 到重启。不要主动制造真实玩家数据丢失。没有实测失败样本时保留“自动测试通过／实机未测”。
6. 验证 Enabled=0、换端口、其它 CWD、配置错误。分别以普通 listener、SO_REUSEADDR listener 占用指定 IPv4／0.0.0.0 的同端口，插件必须失败且不夺取 endpoint。局域网地址只验证明确配置的地址。
7. HTTP 闲置、持续查询、半截请求连接三种条件下正常关闭游戏；确认无挂起／崩溃且端口关闭。spice 约 1 秒的强制终止路径可能截断诊断，不能凭最后一行缺失判断清理成功。
8. 确认 ifs_hook、omnifix、2dx-gsm 原功能正常，请求、卡片、支付、成绩和选曲行为无变化。正式场景关闭采样用 netdump/automap/verbose 参数，证明无日志依赖。需协议对照时另跑不加载本插件的 netdump 基线，避免后装 automap 无法覆盖已包装 slot。

若任一场景缺失必要事件，或发现待机资源预加载／缓存反例，不发布该版本兼容性结论；保持或主动禁用为不可用，收集脱敏事件轨迹、模块导入和相关调用栈。不能用时间阈值、FIFO、猜测 offset 或任意 save 作兜底。

## 发行许可

GPL-3.0-only，见 [LICENSE](LICENSE)。发行二进制时同时提供对应源码与构建说明；CivetWeb 保留其完整 MIT 及第三方许可，Spice SDK 保留上游 GPLv3 许可。运行依赖仍只有 DLL 与 INI，系统 Windows API／UCRT 由系统提供。原始 `iidx-log*.txt` 和 automap XML 是私有研究输入，忽略且不包含在源码包或运行包中。
