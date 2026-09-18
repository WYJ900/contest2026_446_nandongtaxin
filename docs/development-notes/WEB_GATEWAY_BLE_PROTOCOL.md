# Web 网关与 BLE 最小协议

## 目标

为了尽快完成参赛演示，第一阶段使用桌面 Chrome/Edge Web Bluetooth 替代完整
iPhone App。网页承担手机网关职责：校时、速度/距离模拟、读取骑行状态、请求
Xiaomi MiMo 复盘并回传手表。

> iPhone/iPad Safari 没有 Web Bluetooth API，不能直接连接 BLE GATT。若最终必须
> 使用 iPhone，后续只需增加 CoreBluetooth 薄壳；网页服务端和设备协议可继续复用。

## GATT

| 项目 | UUID | 方向 |
|---|---|---|
| Service | `FFF0` | — |
| TX | `FFF1` | 手表 Notify → 网页 |
| RX | `FFF2` | 网页 Write/Write Without Response → 手表 |

帧为 UTF-8 JSON，以换行 `\n` 结束。一个 JSON 可以拆成多次 ATT 写入；手表收到
换行后才解析。网页当前按 18 字节分片发送，手表通知也按 18 字节分片返回，网页
按换行重组。

## 命令

### 连通性

```json
{"cmd":"ping"}
```

返回：

```json
{"ok":true,"event":"pong"}
```

### 手机校时

```json
{"cmd":"time_sync","epoch":1788069600,"timezone_min":480}
```

设备更新系统时间和 RTC，并同步更新 Flash 时间兜底。

### 手机骑行数据

```json
{"cmd":"phone_data","speed":18.6,"distance":3.25}
```

当前只存内存并实时显示，不写 Flash，避免高频磨损。

### 读取骑行状态

```json
{"cmd":"ride_status"}
```

返回字段包含 `active`、`paused`、`motion`、`elapsed_s`、`moving_s`、
`still_s`、`impacts` 和 `latest_sequence`。

### 回传 AI 复盘

```json
{"cmd":"ride_review","text":"本次节奏稳定，下一次注意定时补水。"}
```

受当前 KVDB 单条值大小限制，复盘拆成 5 个 64 字节分块，键名为
`persist.velaride.review.0` 到 `.4`；每块包含序号、块数和 CRC32。手表运动结束页每秒检查一次，收到后
把“本地骑行复盘”替换成“AI 骑行复盘”。开始下一次骑行时清除旧复盘。

## Web 服务

目录：`web-gateway/`

```powershell
cd D:\desktop\VelaRide_AI\web-gateway
npm start
```

浏览器打开 `http://127.0.0.1:4173`。接口：

- `GET /api/health`
- `POST /api/ride/review`

配置 `MIMO_API_KEY` 后调用 OpenAI-compatible `chat/completions`；无 Key、超时或
服务失败时自动使用确定性中文复盘，页面会明确标记来源，不冒充真实 AI。

也支持大赛 Token Plan 的 Anthropic-compatible 接口：通过运行时环境变量
`ANTHROPIC_AUTH_TOKEN`、`ANTHROPIC_BASE_URL` 和 `ANTHROPIC_MODEL=mimo-v2.5`
注入。Token 不得写入仓库或文档。服务端显式关闭长 thinking，网页显示 MiMo
真实原文；若原文包含手表子集字体之外的字符，回传手表使用 `font-safe` 短版。

## USB 串口通道

由于 SF32LB52 BLE HCI 仍阻塞，网页同时支持桌面 Chrome/Edge Web Serial，使用
现有 USB 线和 1000000 波特率完成同一闭环：

- `velaride time-set <epoch>`：校时并保存 Flash 时间兜底；
- `velaride phone-data <speed> <distance>`：写入手机速度/距离；
- `velaride ride-status`：读取实时状态；
- `review-begin / review-part / review-end`：分片回传 UTF-8 复盘；
- `velaride review-read`：诊断读取。

NSH 单行较短，网页将十六进制按 32 字符（16 字节）分片。设备端事务缓冲最多
260 字节，只在 `review-end` 时写入带 CRC 的 KVDB 分块。

浏览器 Web Serial 长连接方案已废弃：CH340 的 RTS-to-RST 接线和长期独占串口会
造成不完整复位、黑屏或断开后主控/串口无响应。进一步真机验证发现，即使本机
服务端短连接也可能使面板再次进入黑态，且必须恢复稳定固件并物理断电才能亮屏。
因此比赛稳定演示中已完全禁用 COM5 运行时网关：

- 骑行选择、开始、暂停、继续、结束全部由手表完成；
- 网页手工录入/使用演示骑行数据并调用真实 MiMo；
- 网页展示真实 AI 复盘，但不连接或复位手表；
- BLE完成后再恢复无线状态读取和回传。

`tools/web_device_bridge.ps1` 和 `/api/device` 仅保留为开发诊断代码，不用于当前
演示，也不应在手表亮屏运行期间调用。

## 已验证

- Node 自动测试：2/2 通过；
- 主页和 API HTTP 冒烟：通过；
- 浏览器页面非空、无错误覆盖层、关键控件齐全：通过；
- 网页演示模式生成复盘并启用回传按钮：通过；
- 设备端新增协议完整编译链接通过；固件大小 `9,642,180 B`，距配置区
  `0x129A0000` 仍有安全余量；
- 复盘字体已扩充常用字符，MiMo 输出被限制到字体可渲染字符；
- `velaride review-demo` 真机写入成功，复盘占前两个分块，复位后分块仍存在；
- 当前演示候选已烧录，`rcS` 仍只启动 `ai_agent + velaride`，不会自动启动未验收 BLE；上电、时间恢复、复盘分块和中文字体已真机验证；
- 真实 BLE 联调仍受 SF32LB52 LCPU HCI TX 提交时序阻塞。
- Web Serial 真机闭环已通过：校时、手机数据、状态读取、155 字节复盘分片回传、
  KVDB 持久化均成功；骑行记录未被覆盖；
- Anthropic-compatible MiMo `mimo-v2.5` 真实请求已通过，浏览器验证显示
  “由 Xiaomi MiMo 生成”。

## 黑屏回归与修复

2026-08-30 的二分真机验证结果：

- `nuttx_rtc_boot_restore_verified_20260830_140118.bin`：亮；
- `nuttx_web_gateway_demo_verified_20260830_145019.bin`：黑；
- 两版 UI 启动、LVGL、LCD 节点和表盘快照日志都正常。

根因是新增的 BLE 768 字节静态接收缓冲和 USB 复盘事务静态缓冲改变了全局内存
布局，使首个 351000 字节表盘快照/帧缓冲地址后移，触发了本板 LCD/PSRAM 的地址
敏感黑屏。修复为：

- BLE RX 缓冲仅在 `velaride_ble` 真正启动时动态分配；
- USB 复盘事务缓冲仅在 `review-begin` 时动态分配，`review-end` 后释放；
- 撤销无效的 LCD VADD 强制掉电实验，恢复已验证初始化顺序。

修复后 SRAM 链接使用从 `428620 B` 降至 `427596 B`，首个快照缓冲恢复到
`0x600bd578` 附近，真机重新亮屏。时间、骑行记录、Agent、Web/MiMo功能均保留。
