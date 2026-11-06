# VelaRide AI 工程总览（2026-11-06 决赛版）

> 给第一次接触本仓的人（包括在其他电脑上剪视频、写说明的 AI）。**读完本页就能理解整个项目**；需要细节时再按“延伸阅读”打开对应交接文档。
> 本页只写真实跑通过的内容；没验证过的单独标注。

## 1. 一句话

立创黄山派（SF32LB52，openvela/NuttX）做的**骑行安全手表**：途中由手表本地完成运动检测、摔倒确认和骑行记录；**真我手机 App** 负责摔倒呼救（短信 + 拨号）、消息/天气转发、定位与路线、寻车；骑完回家插上 USB 充电线，**家里 openvela 设备上的 ai_agent**（演示用官方 goldfish 模拟器）主动读取新记录，跨次对比，把复盘写回手表并做冲击后的安全跟进。

参赛方向：AI 硬件产品创新（组合：手表应用创新）。团队仓 `open-vela/contest2026_446_nandongtaxin`，fork `WYJ900/contest2026_446_nandongtaxin`。决赛 2026-11-07～08，视频演示。

## 2. 系统拓扑

```text
            ┌──────────── 骑行途中（离线） ────────────┐
            │  黄山派手表  ──BLE──  真我手机 App 0.3.6   │
            │  · 运动/冲击/摔倒确认   · 摔倒呼救短信+拨号  │
            │  · 骑行记录(16 条)      · 微信/QQ 消息转发   │
            │  · 消息横幅/天气        · 联网天气 Open-Meteo│
            │  · 路线方向提示         · GPS 轨迹/路线/寻车 │
            └────────────────────────────────────────────┘
                         │ 回家插上 USB 充电线
                         ▼
 [手表] ──USB 串口── serial_logger.py ── web-gateway(MCP 网关) ──MCP── [ai_agent @ openvela 模拟器] ──HTTPS── 大模型
                                              │
                                              └── agent.html 看板（电脑浏览器 / 手机 App“AI 教练”页）
```

- 摔倒呼救**不经过大模型**：手表本地规则 + 手机 App，离线、确定性。
- 大模型只做非紧急的事：复盘文字、跨次对比、安全问候、记忆。
- 黄山派没有 Wi-Fi，手表上的 ai_agent 调不到模型，所以 Agent 跑在模拟器里，执行落在真手表上（“端端协作”）。旁白必须如实说明。

## 3. 当前真机版本

| 部件 | 版本 | 文件 | SHA-256 |
|---|---|---|---|
| 手表固件 | `424e7a2c` | `artifacts/phone-bridge-20261106/firmware.bin` | `424e7a2c41fe57dbd581e1c313bfe8793067415e0af449166700f501d3c1a734` |
| 固件配置 | 与 57e472ce 相同 | `artifacts/phone-bridge-20261106/config` | `88612096…8b99` |
| 手机 App | 0.3.6（versionCode 19） | `artifacts/phone-bridge-20261106/VelaRide-0.3.6-debug.apk` | `aff31a23…c4a3` |
| 模拟器内核 | goldfish-arm64 + ai_agent | 不入库（165 MB） | `vela_ap.elf` `e72ea146…57df` |
| 手表源码 | = `app/velaride/`（与固件逐字一致） | 打包副本 `artifacts/phone-bridge-20261106/app-source.tar.gz` | |

回退：上一版 `57e472ce`（无消息/天气）在 `artifacts/safety-fixes-20261105/firmware.bin`。烧录：

```text
sftool -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after no_reset write_flash --verify firmware.bin@0x12010000
```

## 4. 功能清单与验证状态

| 功能 | 怎么用 | 状态 |
|---|---|---|
| 摔倒检测 → 安全确认 | 4 Hz 读 LSM6DSL，相邻加速度差 ≥650 mg 记撞击；最后一次撞击后 10 s 内静止 3 s → 亮屏、持续响铃、弹“我没事/暂停休息/需要帮助”，30 s 倒计时 | 真机多次触发（11-05、11-06） |
| 摔倒呼救 | 未应答或选“需要帮助” → 手机响铃倒计时 → 定位短信 + 拨打第一位联系人 | 红米上真实摔倒联动成功（11-05）；真我上权限和联系人已设好 |
| 骑行记录与复盘 | 手表保存最近 16 条；骑后复盘页 | 真机（记录已到 #31） |
| 手机消息转发 | App 选应用（微信/QQ）→ 手表横幅“手机新消息”→“手机消息”页卡片 | 11-06 真机实测：微信消息到手表 |
| 天气 | App 连上手表 3 s 后经 Open-Meteo 联网查询（坐标保留 2 位小数）→ 手表消息页顶部天气胶囊 | 11-06 真机实测 |
| 实时看板 | App 每 2 s 读 `ride_status` | 已实现 |
| GPS 轨迹绘制 | App“骑行”页：开定位同步后画出走过的路线（≥3 m 记一点、精度 ≤30 m），蓝牙断开继续记录 | 11-06 室外拍摄 |
| 路线指引 | GPX 导入 / 生成 80 m 见方演示环线 / 把轨迹存为路线 → 手表骑行指南针页底部“直行/左转/右转/偏离 + 距离” | 11-06 室外拍摄；**按轨迹点给方向，不是地图导航，不认路口、不重规划** |
| 停车点 / 寻车 | 手表结束骑行时手机自动记 GPS；寻车用手机指南针箭头 + 直线距离 | 11-06 室外拍摄 |
| ai_agent 回家复盘 | 手表插 USB → 心跳（60 s）读新记录 → 对比记忆中上一条 → `review_write` 写回手表 → 响铃 → 写 MEMORY.md → 有冲击时安全问候 | 11-06 21:40 起自动跑通多次（#29、#30、#31） |
| 用户回复 Agent | 看板回复框 / App“AI 教练”页 → `/api/agent/ask` | 已实现 |

## 5. 代码地图（按重要性）

### 5.1 手表 `app/velaride/`（C，NuttX + LVGL）

| 文件 | 作用 |
|---|---|
| `velaride_ride_session.c/.h` | 骑行状态机、LSM6DSL 采样、撞击/静止判定、安全确认、16 条历史持久化（KVDB）、手机数据与路线提示存储 |
| `velaride_shell.c` | 主界面框架：表盘、菜单、通知中心与横幅、抬腕/熄屏与唤醒遮罩、侧键、`velaride` 串口命令（`ride-status`、`ride-latest`、`ride-pause`、`review-*`、`sound play` 等，供 MCP 网关调用） |
| `velaride_sport_ui.c/.h` | 骑行页、复盘页、设置页、指南针页（路线提示）、“手机消息”页（天气胶囊 + 消息卡片） |
| `velaride_ble.c` | BLE GATT 服务与 JSON 命令：`ride_status`、`ride_history`、`ride_get`、`phone_data`、`nav_hint`、`phone_note`、`phone_weather` 等 |
| `velaride_phone.c/.h` | 手机消息（最多 8 条，仅 RAM）与天气（2 小时有效）存储 |
| `velaride_sound*.c` | 提示音与语音片段播放（安全确认铃声、闹钟音） |
| `velaride_activity.c`、`velaride_memo.c` | 久坐提醒、停车语音标记 |
| `velaride_font_*.c`、`velaride_num_*.c` | 生成的字库（不要手改） |

构建：`artifacts/phone-bridge-20261106/wsl_build.sh`（WSL Ubuntu-22.04 `~/velaride_history_cache`，在 activity-stage 源码上依次打 `artifacts/safety-fixes-20261105/patches/` 的 8 个补丁 + `bridge-5019760.patch` + `apply_bridge.py` + `apply_phone_ui.py`）。

### 5.2 手机 `mobile/android/`（Java，无 Gradle，`build.ps1` 直接 javac/d8/aapt2）

| 文件 | 作用 |
|---|---|
| `MainActivity.java` | 5 个标签页界面（骑行/安全/记录/AI 教练/联动）、连接、定位同步、轨迹、路线、天气、寻车 |
| `BleLink.java`、`PhoneLinkSession.java`、`WatchConnectionService.java` | BLE 连接（前台服务，断线自动重连）、命令请求/应答、消息转发 |
| `EmergencyCenter.java`、`FallAlert.java`、`EmergencyPlan.java`、`EmergencyActivity.java` | 摔倒呼救：倒计时、铃声震动、全屏提醒、短信与拨号 |
| `PhoneNotificationService.java`、`NotificationPolicy.java` | 读取系统通知（用户授权）、过滤验证码/常驻通知、限速去重 |
| `OnlineWeather.java`、`WeatherData.java` | Open-Meteo 天气、WMO 代码转中文 |
| `TrackView.java`、`RoutePlan.java`、`GpsTracker.java` | 轨迹绘制、GPX/演示路线、路线推进与方向提示、GPS 去噪 |
| `ParkingStore.java`、`ParkingSpot.java` | 停车点记录与寻车方向 |
| `RideHistory.java`、`HistorySync.java`、`RideReview.java` | 手机端骑行历史、补传、复盘 |
| `PhoneVendor.java` | 按品牌（真我/红米/华为）适配文字、天气与健康应用 |

### 5.3 网关 `web-gateway/`（Node.js ≥20，无依赖）

| 文件 | 作用 |
|---|---|
| `server.mjs` | HTTP 服务：`/api/ride/review`（AI 复盘）、`POST /mcp`、`GET /api/agent/feed`、`POST /api/agent/ask`、静态页面 |
| `watch-mcp.mjs` | MCP 服务（Streamable HTTP，协议 `2025-03-26`）：6 个手表工具 `ride_status`/`ride_latest`/`ride_pause`/`ride_resume`/`watch_chime`/`review_write`，经 serial_logger 的命令文件与日志和手表交互 |
| `agent-feed.mjs` | 看板数据流：合并 MCP 调用、ai_agent 日志、模拟器 syslog（心跳与工具轨迹）、手表串口（插拔、骑行、响铃、复盘写入） |
| `public/agent.html/.css/.js` | “家庭骑行中枢”看板：执行链路动画、Agent 时间线、跨次对比柱状图、复盘表盘、安全跟进、回复框；手机宽度自适应 |
| `ai-provider.mjs` | OpenAI/Anthropic/MiMo 兼容的复盘调用（App 的在线复盘用） |
| `test/*.test.mjs` | 41 项自动测试（`npm test`） |

### 5.4 Agent Skill `agent_skills/`

| 文件 | 作用 |
|---|---|
| `ride-coach.md` | **本次核心 Skill**：插线后主动复盘 → 跨次对比 → 写回手表 → 响铃 → 记忆 → 冲击安全跟进；暂停/继续必须用户同意；安全边界 |
| `HEARTBEAT.md` | 心跳任务：按 ride-coach 检查，没插线或无新记录时只回 `HEARTBEAT_OK` |
| `ride-safety.md` | 早期 Skill（只读状态、确认后才能暂停） |

### 5.5 模拟器 ai_agent `artifacts/agent-emu-20261106/`

不入库源码树（WSL `~/velaride_agent_emu`），只存补丁与脚本：

| 文件 | 作用 |
|---|---|
| `mcp_toolname_underscore.patch` | **修复**：MCP 工具名 `vr.ride_status` 的点号不符合 OpenAI 工具名规范，中转站返回 502 → 改为 `vr__ride_status` |
| `agent_loop_heartbeat_readfile.patch` | **修复**：心跳回复被缓存（之后每次 0 ms 返回旧文本）；“本地文件工具捷径”把读到的 HEARTBEAT.md/MEMORY.md 原文当回复 → 心跳不缓存，捷径停用 |
| `agent_main_syslog_full.patch` | 主动消息日志不再截断到 128 字节 |
| `heartbeat_60s.patch` | 心跳间隔 30 min → 60 s（**仅演示**） |
| `goldfish-arm64-v8a-ap_defconfig` | 官方 ai_agent defconfig + 3 项（关 JPEG、开 PSEUDOTERM、SCHED_CHILD_STATUS） |
| `build_goldfish.sh`、`export.sh`、`run_emulator.ps1`、`bringup.sh` | 编译、导出、启动模拟器、配置 eth0 |
| `agent_console.py`、`set_llm_key.ps1` | 把命令文件转成 `vela>` 输入；key 只经本机 5999 端口输入，不写日志 |
| `serial_logger.py` | 独占 COM5，记录手表串口、执行命令文件；拔线重连时丢弃排队旧命令 |
| `start_gateway.sh` | 启动网关（MCP + 看板 + 回复接口） |
| `demo_view.py` | 终端版时间线（看板的简化替代） |

## 6. 演示启动顺序

1. `cd backup/phone-realme-20261106 && python -u serial_logger.py COM5 watch_serial.log serial_cmd.txt`
2. `bash backup/agent-emu-20261106/start_gateway.sh`
3. `powershell -File backup/agent-emu-20261106/run_emulator.ps1`，约 30 s 后 `bash backup/agent-emu-20261106/bringup.sh`
4. `python -u backup/agent-emu-20261106/agent_console.py <adb.exe> emulator-5554 backup/agent-emu-20261106/agent.log backup/agent-emu-20261106/agent_cmd.txt`，再往 `agent_cmd.txt` 追加 `mcp_add vr http://10.0.2.2:4173/mcp` 和 `mcp_discover`
5. 手机 USB 连电脑：`adb reverse tcp:4173 tcp:4173`（每次重装 App 或重插都要重做）
6. 浏览器 `http://127.0.0.1:4173/agent.html`；手机 App“AI 教练”页

## 7. 视频素材脚本（给剪辑）

建议总长 ≤8 分钟，按“问题 → 方案 → 演示 → 技术 → 边界”：

1. **问题**（30 s）：独自骑行摔倒没人知道；骑行中看手机危险。
2. **途中：手表本地安全**（90 s）：开始骑行 → 甩一下扣下 → 手表亮屏响铃弹安全确认 → 不应答 → 手机倒计时、短信、拨号。强调“离线、确定性、不经过大模型”。
3. **途中：手机联动**（90 s）：微信消息到手表横幅和消息卡片；手表顶部天气；App 轨迹实时绘制；手表指南针页路线提示；结束骑行自动记停车点，寻车箭头。
4. **回家：ai_agent 主动复盘**（90 s）：插上 USB → 不用任何操作 → 看板时间线逐步出现（心跳 → 读 Skill/记忆 → 读取骑行记录 → 复盘写回手表 → 响铃 → 写入记忆）→ 跨次对比卡片“比上次 ±N 秒”→ 安全跟进“人和车都还好吗” → 手表响铃、复盘页出现同样文字 → 用户回复“我没事”。
5. **技术**（60 s）：openvela 官方 ai_agent 框架 + 自定义 Skill `ride-coach` + MCP 6 个手表工具 + 心跳主动 + 记忆；修了框架 3 个问题（工具名 502、心跳缓存、读文件捷径）。
6. **边界**（30 s）：Agent 跑在 openvela 模拟器、执行在真手表；路线是轨迹点方向提示不是地图导航；手机熄屏后蓝牙可能卡死需重启手表。

旁白必须如实：“Agent 跑在 openvela 官方模拟器的 ai_agent 框架里，用自定义 ride-coach Skill，按心跳主动工作，通过 MCP 在真手表上执行；摔倒呼救由手表本地规则处理，不经过大模型。”

## 8. 已知问题（如实说明，不要掩盖）

- **蓝牙卡死**：手机熄屏约 1 分钟或频繁断连后，手表 BLE 接收线程可能停住；11-06 室外一次出现“蓝牙断开 + 触摸无反应、侧键有反应”，原因未查明（当时手表未接 USB，没有日志）。恢复：按住侧键重启。演示时手机亮屏、App 前台。
- **插 USB 会让手表重启**（CH340 复位线耦合），所以必须先在手表上结束骑行再插线。复位后时间靠手机连接校时。
- 路线提示是 GPX 几何方向，没有地图、路口识别和重规划；GPS 误差 5–15 m。
- 心跳 60 s 是演示值；真实场景应为分钟级或插线事件触发。
- Agent 复盘偶尔没写安全问候（21:38 那次），已在 Skill 里改为必写，后续 #30、#31 均带“请检查车况”。
- 消息正文中手表字库外的生僻字会被略去。
- 天气依赖手机联网；真我系统天气不对第三方开放，所以用 Open-Meteo。

## 9. 延伸阅读

- `docs/HANDOFF_AGENT_20261106.md`：Agent、模拟器、App 0.3.x、本日全部改动（0.1–0.3 节是最新）
- `docs/HANDOFF_SAFETY_20261105.md`：摔倒检测怎么工作（第 5 节）、蓝牙卡死分析（第 6 节）
- `docs/HANDOFF_STABLE_20261105.md`：稳定功能版与卡顿根因
- `docs/FEATURES_20261104.md`：手表各功能使用路径
- `docs/KNOWN_LIMITATIONS.md`、`docs/VALIDATION.md`：历史限制与验证边界
- `README.md`：仓库入口、编译、Web 网关
