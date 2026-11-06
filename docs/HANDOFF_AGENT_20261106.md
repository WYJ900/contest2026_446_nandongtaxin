# ai_agent 骑行教练与手机端交接（2026-11-06 晚）

**新对话先读本页。** 接续 [摔倒检测修复交接](HANDOFF_SAFETY_20261105.md)。决赛 11 月 7–8 日（8 分钟演讲 + 2 分钟问答），**用视频演示**，所以只要每段功能真实跑通并录下来即可。

## 0. 一句话现状

- 手表固件**仍是 `57e472ce`，本轮没改**。
- 手机换成**真我 GT Neo6 SE**，装了 **App 0.3.5**。
- openvela **goldfish 模拟器里的 ai_agent 已跑通**：联网，能调大模型（`sub.micuapi.ai`，模型 `gpt-6-sol`，key 由用户输入），并通过 MCP 发现了真手表的 6 个工具。
- **还没做**：让 Agent 真正回答一次；心跳主动提醒 →（用户同意后）手表暂停 → 复盘写回手表，这整条链路一次都没跑过。
- **本轮所有改动都没提交 git**（上一次提交是 `9e14b99`）。

### 0.1 续（18:25–18:57）：修了 ai_agent 框架 3 个问题，对话/工具/心跳/记忆已实测通过

| 现象 | 根因 | 修法（只改模拟器树 `~/velaride_agent_emu/packages/ai_agent`，原文件留 `.orig`） |
|---|---|---|
| 每次 `ask` 都 `Sorry, I encountered an error`，syslog `API error 502` | MCP 工具名带点号 `vr__ride_status`，不符合 OpenAI 工具名 `[a-zA-Z0-9_-]`。电脑端同一请求：点号 → 502（43 s），`vr__ride_status` → 200（2 s） | `src/tools/mcp_client.c`：前缀改为 `server__tool`，调用时按服务名长度剥前缀。补丁 `backup/agent-emu-20261106/mcp_toolname_underscore.patch` |
| 心跳第 1 次把 HEARTBEAT.md 原文当回复；之后每次 `Cache hit, skipping LLM call`，0 ms 返回旧文本 | ① `agent_loop.c` 的“本地文件工具捷径”把 `read_file` 结果直接当最终回复；② 固定心跳 prompt 被当“简单问题”写进回复缓存 | `src/core/agent_loop.c`：心跳会话（`chat_id=="heartbeat"`）不读写缓存；捷径去掉 `read_file`（读文件几乎总是中间步骤），原“技能目录例外”随之删除。补丁 `agent_loop_heartbeat_readfile.patch` |
| “记住 40 分钟”时 Agent 回了 MEMORY.md 原文，没写入 | 同上捷径：先 `read_file` 再写，读完就被截断 | 同上 |

- Skill 里的工具名已全部改为 `vr__*`（`agent_skills/ride-coach.md`、`HEARTBEAT.md`），并已推到模拟器。
- 新内核 `vela_ap.elf` SHA-256 `adf48ee8…f375b`（旧的 `d23978c1…fa70` 备份在 `backup/agent-emu-20261106/prev_kernel_dotname/`）。只替换了 `nuttx`/`vela_ap.*`，**`vela_data.bin` 没覆盖**，key 和 Skill 都还在。
- 实测通过（证据：`agent.log`、`emulator_console.log`、`watch_serial.log`）：
  - `ask 你好…` → 正常中文回答；
  - `ask 读一下手表现在的骑行状态` → `vr__ride_status` → 串口 `velaride ride-status` → 回答与手表真实数据一致（未在骑行）；
  - 心跳（60 s）→ 读 HEARTBEAT.md → 读 ride-coach.md → `vr__ride_status`（串口 18:48:12）→ 未骑行 → `HEARTBEAT_OK`；
  - 记忆：`ask 我一般骑 40 分钟就累，请记住这一点` → `read_file` → `write_file`，MEMORY.md 写入“用户通常骑行约 40 分钟就会感到疲累”（存档 `MEMORY_after_memory_test.md`）。回复是 `OK: wrote 65 bytes…`（写文件捷径的原样输出，不好看但真实）。
- **测完已把 MEMORY.md 恢复为初始内容**：否则心跳会按 40 分钟阈值等，2 分钟演示不会触发。录视频时**记忆镜头放最后拍**。
- 心跳的提醒文字只出现在 syslog（`emulator_console.log` 里 `System message [heartbeat]: …`），不在 `agent.log`；录屏要带上模拟器窗口。
- 19:00–19:16 真实骑行（记录 #20）实测：心跳在运动 164 s 时调用 `vr__watch_chime`（19:03:38 手表播放），`ask 好，暂停吧` → 手表 `paused:true`（19:04:19），`ask 继续骑` → resume（19:08:16），手表上结束 → `ask 帮我复盘` → `review_write` 写入手表。用户反馈：**没注意到响铃**，只看到后来暂停了。
- 随后又修了 3 处：
  - 第 3 处 ai_agent：最后一步是写记忆时，提醒文字被 `OK: edited MEMORY.md` 顶掉 → 把“本地文件工具捷径”整段停用（同一补丁文件已更新）。新内核 SHA-256 `dd09258b…4d08`。
  - 网关 `watch_chime` 的正则里是**字面退格符**（`/ret=0^H/`，应为 ``），永远判失败，Agent 因此说“提示音未能响起”。已修，新增测试 1 项，网关共 37 项全过。
  - 响铃原来播 `ride` 提示音，内容是语音“进入骑行模式”，不像提醒。改为 `alarm`：闹钟音连响 4 次，约 9 s 自动停（19:16:03–19:16:12 实测）。
- 新增 `backup/agent-emu-20261106/demo_view.py`：把对话、心跳提醒、工具调用和手表动作合并成一条中文时间线，**录屏用这个窗口**（模拟器窗口本来就是黑的）。启动命令：`python -u backup/agent-emu-20261106/demo_view.py`。

### 0.2 续（19:30–20:45）：场景改为“回家插线复盘”；手表固件加回消息+天气；App 0.3.6 分页

**手表固件已换**：`backup/phone-bridge-20261106/firmware-424e7a2c.bin`（SHA-256 `424e7a2c…a734`，9,807,228 字节，配置与 57e472ce 相同）。
- = 57e472ce 的同一构建脚本 + 提交 `5019760` 的手表端消息/天气（`phone_note`、`phone_weather` 等 4 条 BLE 命令、`velaride_phone.c`、横幅）+ 新排版的“手机消息”页（天气胶囊 + 每条消息一张卡片）。去掉了该提交里无关的 TF SPI 探测。
- 构建：`my-contest-repo/artifacts/phone-bridge-20261106/wsl_build.sh`（`wsl -d Ubuntu-22.04 -- bash -s < 脚本`）。
- **回退**：`backup/wake-shield-57e472ce/firmware.bin`，`sftool -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after no_reset write_flash --verify <bin>@0x12010000`，再 `!reset`。烧录前先停 serial_logger。
- 已实测：开机自检（屏、触摸、历史 #20、BLE 广播）；微信消息到手表横幅和消息页；联网天气显示在手表。
- 回归：用户 20:50 测试后表示“没问题”（未逐项列出）；蓝牙熄屏卡死是否变化未专门测。

**App 0.3.6**（`exports/mobile/VelaRide-0.3.6-debug.apk`，`7CEC8E1F…`，versionCode 19，已装真我）：
- 底部 5 个标签：骑行 / 安全 / 记录 / AI 教练（WebView 打开 `http://127.0.0.1:4173/agent.html`，可回复 Agent）/ 联动。
- 联网天气：Open-Meteo（坐标保留 2 位小数），连上手表 3 s 后自动同步，也可手动。
- 通知监听：ColorOS 更新 App 后不会重新绑定，启动时禁用再启用组件强制重新绑定（已验证）。
- 演示路线：以当前 GPS 为起点生成 80 m 见方环线（未实测）。
- 每次重装或 USB 重连后要重新 `adb reverse tcp:4173 tcp:4173`，否则 AI 教练页连不上。

**Agent 场景**：家里的 openvela 设备（模拟器）+ 手表插 USB 充电线时主动复盘、跨次对比、冲击跟进；拔线时安静。`serial_logger.py` 重连时丢弃排队旧命令。网关用 `backup/agent-emu-20261106/start_gateway.sh` 启动（含看板数据流和 `/api/agent/ask`）。**完整“拔线骑行 → 插线复盘”还没实拍过。**

### 0.3 21:40 “插线复盘”已自动跑通一次

- 用户室外测试时手表存了 #25–#29；21:37 插回 USB，手表**会自动重启**（20:58、21:37 两次都这样，所以必须先在手表上结束骑行再插线）。下一次心跳读到 #29，与记忆里的 #23 对比，复盘写回手表、响铃、记进记忆，共 42 s，无人操作。
- 写回手表的复盘原文：“本次20秒，运动10秒；比上次少25秒，冲击少2次。记录到2次冲击，请检查车况。下次留意路况。”
- 问题：这次回复里漏了冲击后的安全问候 → `ride-coach.md` 改为必须加第三句（已推到模拟器，未再次验证）。
- 看板修复：工具调用失败的 `{ok:false}` 曾被当成骑行记录（复盘编号显示为空）→ 只收带 sequence 的记录，网关测试 41 项通过，网关已重启。

## 1. 为什么做这个（评分依据）

官方 AI 硬件方向的基础要求有两条：**至少 1 个自定义 Skill，并演示使用**；**用 ai_agent 框架实现至少 1 个“主动 + 执行”场景**，纯对话机器人不算。
我们已有的摔倒呼救是手表原生 C 代码加手机 App 实现的，**不是 ai_agent 在执行**。黄山派没有 Wi-Fi，手表上的 ai_agent 调不到大模型（开机日志里有 `Network timeout`）。
所以采用的方案是：

> **Agent 跑在 openvela 官方模拟器里（真框架、真模型）→ 通过 MCP 调用电脑网关 → 网关经 USB 串口在真手表上执行。**

这也对应指引里的加分项“端端协作”。旁白要如实说“Agent 跑在 openvela 模拟器，执行落在真表”。
摔倒呼救**不交给大模型**，仍由手表本地规则处理（离线、确定性）。Agent 只负责建议、非紧急的执行和复盘。

## 2. 拓扑与启动顺序（每次重新开始都按这个顺序）

```
[真我手机] --BLE-- [手表 57e472ce] --USB COM5-- serial_logger.py
                                                   ↑ 命令文件 / 日志
[模拟器 ai_agent] --MCP http://10.0.2.2:4173/mcp--> [电脑网关 web-gateway]
```

1. **串口录制**（独占 COM5）：
   `cd backup/phone-realme-20261106 && python -u serial_logger.py COM5 watch_serial.log serial_cmd.txt`
2. **网关**（开启 MCP，指向上面两个文件）：
   ```
   cd my-contest-repo/web-gateway
   VELARIDE_MCP=1 VELARIDE_SERIAL_CMD=<绝对路径>/serial_cmd.txt VELARIDE_SERIAL_LOG=<绝对路径>/watch_serial.log PORT=4173 node server.mjs
   ```
   如果还要录 AI 复盘，改用 `start-ai.ps1` 启动，并在同一个进程里设好上面三个环境变量。
3. **模拟器**：`powershell -File backup/agent-emu-20261106/run_emulator.ps1`。约 10 秒后出现 NSH。
4. **开机准备**：`bash backup/agent-emu-20261106/bringup.sh`。
   它会给 eth0 配静态地址 `10.0.2.15`、网关 `10.0.2.2`、DNS `10.0.2.3`（这一版 NETINIT 开机后 eth0 是 0.0.0.0），并检查 Skill 文件。
5. **Agent**：每次开机**只能启动一个实例**，否则第二个实例的命令行收不到输入：
   `python -u backup/agent-emu-20261106/agent_console.py <adb.exe 绝对路径> emulator-5554 backup/agent-emu-20261106/agent.log backup/agent-emu-20261106/agent_cmd.txt`
   - 往 `agent_cmd.txt` 追加一行，就是给 `vela>` 输入一条命令；输出看 `agent.log`。
   - 转发程序会拒绝从命令文件传入的 `set_llm`。key 只能通过 `set_llm_key.ps1` 输入（本机 5999 端口，不写日志）。
6. **每次开机都要重新**执行 `mcp_add vr http://10.0.2.2:4173/mcp` 和 `mcp_discover`（MCP 配置不会保存）。
   大模型配置会保存在模拟器的 `/data/ai_agent/config`，重启后不用重新输入。前提是 `vela_data.bin` 没有被重新编译覆盖；覆盖了就重跑 `set_llm_key.ps1`。

## 3. 下一步（按顺序）

1. **验证模型**：`ask 你好，用一句话介绍你自己`。如果报错：
   - 先确认中转站是否兼容 `/v1/chat/completions`，以及 `gpt-6-sol` 能不能用；
   - 还不行就换 MiMo：`set_llm_key.ps1` 里输入 `mimo`。
2. **工具链路**：`ask 读一下手表现在的骑行状态`。预期结果：Agent 调用 `vr__ride_status`，网关日志出现 `[mcp] tools/call ride_status`，串口日志里出现 `velaride ride-status`。
3. **主动 + 执行**（要录的核心镜头）：
   - 手表上开始骑行，让它保持运动约 2 分钟（演示阈值是 `moving_s ≥ 120`）。
   - 心跳**每 60 秒**一次（只在模拟器固件里改的，见第 5 节），也可以手动 `heartbeat_trigger`。
   - 预期：Agent 调用 `vr__watch_chime`（手表响），然后问“要我在手表上暂停吗？”。
   - 回复 `ask 好，暂停吧` → 调用 `vr__ride_pause` → 手表屏幕显示暂停。
4. **复盘写回手表**：手表上结束骑行 → `ask 帮我复盘` → `vr__ride_latest` → `vr__review_write` → 在手表复盘页查看。
5. **记忆**（加分项）：`ask 我一般骑 40 分钟就累` → 检查 `/data/ai_agent/memory/MEMORY.md` 里有没有记下来。
6. **视频镜头**：模拟器窗口 + `agent.log` + 手表实拍（响铃、暂停、复盘页）+ 网关日志，旁白如实说明拓扑。
7. **提交 git**（见第 7 节清单）。

心跳 prompt 是英文固定句，要求读 `/data/ai_agent/HEARTBEAT.md` 并照做；没事要做时回复 `HEARTBEAT_OK`。
**已验证（18:48）**：心跳会先读 HEARTBEAT.md、再读 ride-coach.md，然后调用 `vr__ride_status`。达到阈值后是否会调 `vr__watch_chime` 还要等真实骑行验证。

## 4. 本轮文件与改动

**模拟器构建（只在模拟器专用源码树里，手表构建树没碰）**

- WSL `Ubuntu-22.04`，源码 `~/velaride_agent_emu`：从 `~/velaride_history_cache` rsync 而来。**手表用的 `velaride_history_cache` / `velaride_safety_stage` 都没改**。
- 工具链：`~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf`（Linux 版 GCC 13.3，ARM64）。
- 配置：`~/agent_emu_cfg/goldfish-arm64-v8a-ap/defconfig`，基于官方 `packages/ai_agent/defconfigs/goldfish-arm64-v8a-ap_defconfig`，只多了三项：
  - `# CONFIG_LV_USE_LIBJPEG_TURBO is not set`：用不到 JPEG 解码，开着会链接失败；
  - `CONFIG_PSEUDOTERM=y`、`CONFIG_SCHED_CHILD_STATUS=y`：让官方配置里的 `ADBD_SHELL_SERVICE` 真正生效。
- 编译参数多了 `-Wno-error=unused-function`：我们自己的 libuv 补丁在 ARM64 上会报“函数未使用”。
- 构建脚本：`backup/agent-emu-20261106/build_goldfish.sh`，用 `wsl -d Ubuntu-22.04 -- bash -s < 脚本` 运行。增量编译约 20 秒，全量约 10 分钟。
- 在副本里修好的问题：
  - 409 个被压扁成普通文件的符号链接，用 `fix_links.py --fix` 修复；
  - 从 Windows 树复制了 `external/android`、`googletest`、`libjpeg-turbo`、`unqlite`；
  - 用 `git lfs pull` 拉下 `vendor/openvela/boards/vela/libs/ap_cmake/libgui_wrapper.a` 和 `libquickapp.a`，SHA-256 与 LFS 指针一致。Windows 树里的这两个文件也因此变成了真实文件。
- `ai_agent` 只改了一处：`agent_config.h` 的心跳间隔从 30 min 改为 60 s，补丁见 `heartbeat_60s.patch`，**只用于演示**。
- 在 WSL 里 `sudo apt install mtools` 装了 `mcopy`，用来生成镜像。
- 产物在 Windows 的 `cmake_out/vela_goldfish-arm64-v8a-ap/`：`vela_ap.elf` 约 165 MB，`vela_data.bin` 256 MB。用 `export.sh` 复制过来。

**MCP 服务（`my-contest-repo/web-gateway/`）**

- 新增 `watch-mcp.mjs`：MCP Streamable HTTP，返回纯 JSON，协议版本 `2025-03-26`。
  - 工具：`ride_status`、`ride_latest`、`ride_pause`、`ride_resume`、`watch_chime`、`review_write`。
  - 实现方式：往 `serial_cmd.txt` 追加 `velaride ...` 命令，再从 `watch_serial.log` 读回复；同一时间只处理一条命令，6 秒超时。
  - `review_write` 会过滤掉手表字库外的字符，按 32 字节分段走 `review-begin/part/end`。
- `server.mjs` 新增 `POST /mcp`，只在 `VELARIDE_MCP=1` 且设置了串口文件时开放，其余情况返回 403。
- 新增 `test/watch-mcp.test.mjs`，4 项；网关测试一共 36 项，全部通过。
- 已在真手表上测过：`ride_status`、`ride_latest` 返回真实数据（最新记录 seq 19）；`ride_pause` 在没有骑行时返回 `no_active_ride`。
- ai_agent 的 MCP 客户端会给工具名加前缀，例如 `vr__ride_status`（原为点号 `vr.`，会导致 502，已改，见 0.1 节）。内置的 `fetch_url` 只支持 https，而且会拦截局域网地址，所以**不能用它来连网关**。

**Skill（`my-contest-repo/agent_skills/`）**

- 新增 `ride-coach.md`：心跳主动检查 → 达到阈值就响铃并提醒 → 用户同意后才执行暂停/继续 → 结束后复盘写回手表 → 把阈值和每次骑行记进 MEMORY.md。安全边界里写明了摔倒不归它管。
- 新增 `HEARTBEAT.md`：一条任务，按 ride-coach 做主动检查。
- 这两个文件已推到模拟器的 `/data/ai_agent/skills/ride-coach.md` 和 `/data/ai_agent/HEARTBEAT.md`。原有的 `ride-safety.md` 保留。

**手机 App（`my-contest-repo/mobile/android/`，真我上装的是 0.3.5）**

| 版本 | 内容 |
|---|---|
| 0.3.2 | 骑行实时看板（`RideLive`，后台服务每 2 s 推一次 `ride_status`）；寻车（`ParkingSpot` / `ParkingStore`：结束骑行时自动记 GPS，用手机指南针显示箭头和距离，**不发给手表**） |
| 0.3.3 | `PhoneVendor`：按品牌自动适配。真我会隐藏“同步系统天气”；健康应用依次找欢太、华为；新增“关闭电池优化” |
| 0.3.4 | 手表失联 60 s 后弹出高优先级通知（`watch_lost`），标题“请重启手表” |
| 0.3.5 | AI 复盘地址允许 `http://127.0.0.1`（`network_security_config` 只对本机回环放开明文），配合 `adb reverse tcp:4173 tcp:4173` 经 USB 访问电脑网关；“最近骑行复盘”改为打开编号最大的那条 |

- 新增测试：`RideLiveTest`、`ParkingSpotTest`；`ReviewClientTest` 增加了回环地址的用例。主机测试全部通过。
- APK 在 `exports/mobile/VelaRide-0.3.5-debug.apk`，SHA-256 `C76E4FCE…`。
- 真我上实测过：
  - 权限全部通过，紧急联系人 1 位，电池优化已关闭，手表复位后 8 s 内自动重连；
  - 经 USB 请求网关复盘，返回的是**规则降级**结果（当时网关没配 key）。
- 还没测：寻车的指南针箭头和结束骑行自动记点；看板跟随真实骑行；真模型的 AI 复盘回传手表。

## 5. 新发现的固件问题（没修，按用户决定只在手机端规避）

- **手机熄屏约 1 分钟后，手表蓝牙会卡死**（真我上 10 分钟内出现 2 次）。卡在 `sf32lb52_bt_rx_worker` 收数据这一层：
  - `rx_ind pending=1 running=1` 之后再没有 `ipc_rx`，邮箱中断本身正常；
  - 手表收不到断开事件，也就不会重新广播，只能插拔或复位手表恢复。
  - 证据在 `backup/phone-realme-20261106/watch_serial.log`（16:37、16:43），分析写在 `HANDOFF_SAFETY_20261105.md` 第 6 节。
- **录视频时手机保持亮屏、插电**。开录前插拔一次手表。

## 6. 真我 GT Neo6 SE 要点

- RMX3850，realme UI 7.0 / ColorOS 16，Android 16（SDK 36），adb 序列号 `79a6b144`。adb 能直接模拟点击。
- 耗电管理和自启动页面需要 `oplus.permission.OPLUS_COMPONENT_SAFE`，第三方 App 打不开，只能跳到应用详情页。
- 系统天气的 provider 不对第三方开放。
- 手机 GPS 和网关都可以经 USB 使用：`adb reverse tcp:4173 tcp:4173`。

## 7. 待提交 git 清单（提交前先 `git status`）

- `mobile/android`：0.3.2–0.3.5 的全部源码、测试和 `build.ps1`；新增 `res/xml/network_security_config.xml`。
- `web-gateway`：`watch-mcp.mjs`、`server.mjs`、`test/watch-mcp.test.mjs`。
- `agent_skills`：`ride-coach.md`、`HEARTBEAT.md`。
- `docs`：`HANDOFF_SAFETY_20261105.md`（第 6、7 节已更新）、本页。
- 建议归档到 `artifacts/agent-emu-20261106/`：
  - 模拟器 defconfig；
  - `heartbeat_60s.patch`；
  - `build_goldfish.sh`、`fix_links.py`、`run_emulator.ps1`、`bringup.sh`、`agent_console.py`、`set_llm_key.ps1`；
  - 0.3.5 APK 及其哈希。
  - 165 MB 的 elf 和 256 MB 的 data.bin **不要提交**，只记哈希。
- **不要提交**：`artifacts/_tmp_gps_fresh/`（来源不明）；任何带 key 的文件。`agent.log` 已确认不含 key。

## 8. 后台进程（本对话结束时还在运行）

- `serial_logger.py`（COM5）；
- 网关 `node server.mjs`（4173，已开 MCP，**没配复盘用的 AI key**）；
- 模拟器 `emulator.exe`；
- `agent_console.py`（5999 端口收 key）。

新对话开始前，用 `Get-CimInstance Win32_Process` 查一下哪些还在，按需复用，不要重复启动。
