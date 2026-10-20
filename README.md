# cabinet-status _(konami-cabinet-status)_

[![KCS Latest Release](https://github.com/GoldBro233/konami-cabinet-status/actions/workflows/release.yml/badge.svg)](https://github.com/GoldBro233/konami-cabinet-status/actions/workflows/release.yml)

Show the status of konami cabinet

它通过 spice2x 插件观察 beatmania IIDX 与 SOUND VOLTEX 机台上是否有人游玩，并以 HTTP JSON 暴露给外部程序：玩家刷卡登录或 IIDX 进入游客流程后返回 `occupied`，回到待机流程后返回 `free`。IIDX 支持同一机台的两份玩家档案，接口报告整机占用。

## 内容列表

- [cabinet-status _(konami-cabinet-status)_](#cabinet-status-konami-cabinet-status)
  - [内容列表](#内容列表)
  - [背景](#背景)
  - [安装](#安装)
    - [配置](#配置)
  - [使用说明](#使用说明)
  - [工作原理](#工作原理)
  - [兼容性](#兼容性)
  - [从源码构建](#从源码构建)
  - [已知限制](#已知限制)
  - [API](#api)
    - [`GET /status`](#get-status)
  - [使用许可](#使用许可)

## 背景

机台屏幕不在视线内，或想让外部面板、预约系统显示“空闲／占用”时，需要一个可查询的状态来源。IIDX 与 SOUND VOLTEX 都在 Windows 上通过 spice2x 运行，插件可以直接从游戏进程内部观察玩家的登录与退出。

## 安装

需要 Windows x64 与可正常启动游戏的 spice2x（含 AVS core）。

1. 从 [Releases](https://github.com/GoldBro233/konami-cabinet-status/releases/latest) 下载 `cabinet-status.dll` 与 `cabinet-status.ini`，放进现有插件目录（与 `ifs_hook.dll`、`2dx-gsm.dll` 同级）。

2. 保持原有 spice2x 参数，把 `cabinet-status.dll` 追加到 `-k` 列表末尾：

   ```
   -k ... cabinet-status.dll
   ```

3. 启动游戏，在 spice2x 日志中搜索 `cabinet-status`，确认出现 `listening=127.0.0.1:18080/status`。

4. 向 `http://127.0.0.1:18080/status` 查询。

升级时只替换 DLL，保留自己改过的 INI。

### 配置

```ini
[Server]
Enabled = 1
Port = 18080
BindAddress = 127.0.0.1
```

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `Enabled` | `1` | 设为 `0` 时不安装 hook、不监听端口 |
| `Port` | `18080` | 十进制 `1..65535` |
| `BindAddress` | `127.0.0.1` | 仅接受四段十进制 IPv4；局域网访问改为网卡地址或 `0.0.0.0` |

## 使用说明

```console
$ curl -s http://127.0.0.1:18080/status
{"status":"free"}
```

玩家刷卡登录，或 IIDX 进入游客流程后：

```console
$ curl -s http://127.0.0.1:18080/status
{"status":"occupied"}
```

外部面板定时轮询即可；响应带 `Cache-Control: no-store`，不需要时间戳参数。遇到 503 应显示为“未知”，而不是“空闲”。

| 响应 | 含义 |
| --- | --- |
| `200` + `{"status":"free"}` | 已返回待机流程 |
| `200` + `{"status":"occupied"}` | 玩家已登录、IIDX 游客已进入，或正在结算／登出 |
| `503` + `{"error":"state_unavailable"}` | 尚不能判断：初始化中、等待游戏响应、环境不支持或采集已失效 |

## 工作原理

以 IIDX 为例：

1. 待机画面加载完成 → `free`
2. 游戏成功返回任一玩家档案，或游客发出 `IIDX33pc.playstart`／`IIDX34pc.playstart` 请求 → `occupied`
3. 玩家登出或保存成绩 → 仍为 `occupied`；单侧结束不会单独释放整机
4. 回到待机画面，游戏重新打开并挂载 `/data/graphic/0/sub_demo.ifs` 或 `/data/graphic/1/sub_demo.ifs` → `free`（允许路径中的重复斜杠）

IIDX 同时保留最多两份已加载档案，允许两个档案请求交错完成。档案响应以自身 ID 识别玩家，保存与登出分别关联对应档案；游客进入信号不覆盖有卡玩家的档案。所有会话统一在上述整机待机资源边界清除。

IIDX 档案响应必须包含 `status="0"` 和有效档案 ID；允许省略 `fault`，显式非零的 `fault` 不视为登录成功。

SOUND VOLTEX 的正常刷卡流程使用相同的状态机：`sv7_load` 的完整成功档案响应确认占用，以请求中的 `refid` 关联后续保存；单曲保存 `sv7_save_m` 和总结果上报 `sv7_save_e` 保持占用，最终 `sv7_save` 进入结束阶段，重新打开并完成 `/data/graphics/ver07/title.ifs` 的 `imagefs` 挂载后释放。该资源在 `TITLEDEMO_FLOW` 内预加载，`free` 表示已返回待机流程，不保证标题画面已经显示完毕。PASELI 会话关闭不直接释放占用。

档案加载失败、中断或响应无法解析时，接口暂时返回 503。游戏重新成功打开并挂载待机资源后，插件清除当前会话并恢复 `free`，随后可以再次登录；不会按超时推断空闲。

## 兼容性

| 游戏 | model | 状态 |
| --- | --- | --- |
| beatmania IIDX 33 | `LDJ` | 单人已实测；游客与双人已适配，待新 DLL 实机验证 |
| beatmania IIDX 34 | `LDJ` | 用户已确认修复版正常可用；游客与双人待实机验证 |
| SOUND VOLTEX | `KFC` | 已适配 `sv7` 协议正常刷卡流程；待 Windows 实机验证 |
| 其它 | — | 返回 503 |

AVS core `2.17.0.0`、`2.17.3.0`、`2.17.4.0`（`XCgsqzn/2.17` 导出族）可识别；未知版本或缺少所需导出时保持 503。

实测环境为 IIDX 33、spice2x `1.0-V-2026-09-12`、AVS core `2.17.4.0`，与 `ifs_hook`、`omnifix`、`2dx-gsm` 同时加载。其它游戏版本与插件组合需自行验证。

IIDX 33 与 34 分别识别 `IIDX33pc` 和 `IIDX34pc`，无需切换配置。两种服务名的登录、保存、登出和游客流程已通过构造协议数据及实际 HTTP 查询验证。IIDX 34 的 `iidx-34-01.txt` 已确认单人登录、保存和 AVS core `2.17.4.0`；该环境使用目录 `0` 的待机资源，修复后已通过日志事件回放，用户已确认修复版正常可用。

## 从源码构建

需要 CMake 3.25+、Ninja、MinGW-w64 x64、Python 3 和 LLVM 工具。

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
python3 tests/http_smoke.py --server build/linux-debug/http-smoke-host

cmake --preset windows-release
cmake --build --preset windows-release
cmake --install build/windows-release --component Runtime --prefix out/windows-x64
```

## 已知限制

- IIDX 接口只报告整机占用。游客及双人修复尚待新 DLL 实机验证，单侧中途退出、双人保存和有卡／游客混合流程尚无完整实机证据。
- SOUND VOLTEX 当前仅适配 `sv7` 协议与 `ver07/title.ifs` 待机资源；取消登录、游客、新卡注册、连续多曲及支付／网络异常流程未验证。
- 导入表被其它插件替换、必要访问器缺失等采集故障发生后，本进程内状态保持 503，需要重启游戏才能恢复。IIDX 允许最多两个并行档案请求与两份活动档案，SOUND VOLTEX 仍只处理单个档案会话。
- 端口被占用（例如已有一个实例在跑）时，插件记录错误并停止服务，不会接管端口。
- 端口、路径与响应格式固定，没有写接口、CORS 或热重载。

## API

### `GET /status`

| 情况 | HTTP | 响应体 |
| --- | --- | --- |
| 待机 | `200` | `{"status":"free"}` |
| 已登录／结束中 | `200` | `{"status":"occupied"}` |
| 无法判断 | `503` | `{"error":"state_unavailable"}` |
| 其它路径 | `404` | `{"error":"not_found"}` |
| `/status` 的非 GET 方法 | `405` | `{"error":"method_not_allowed"}` |

- 接受并忽略查询串，例如 `/status?t=1`。
- 响应均为 `Content-Type: application/json`、`Cache-Control: no-store`、`Connection: close`；非 HEAD 响应带精确 `Content-Length`。
- `405` 附带 `Allow: GET`。`HEAD` 省略响应体，其它路径返回 404，`/status` 上仍返回 405。
- 无 CORS 头。

## 使用许可

[GPL-3.0-only](./LICENSE)。分发二进制时需同时提供对应源码；CivetWeb 的 MIT 许可与 Spice SDK 的 GPLv3 许可见 [third_party/README.md](third_party/README.md)。
