# 摔倒检测修复与手机联动交接（2026-11-05 晚）

**新对话先读本页。** 接续 [稳定版交接](HANDOFF_STABLE_20261105.md)。决赛 11 月 7–8 日。
本轮在用户确认的稳定版 `661393da` 上修好了摔倒检测和“结束骑行卡死”，下一步做手机 App。

## 1. 手表当前固件

| 项 | 值 |
|---|---|
| 文件 | `artifacts/safety-fixes-20261105/firmware.bin` |
| SHA-256 | `57e472ce952161ab2876acc4b9feedf90cfbf0093c0ebf6f773de82a9d34a436`（9,803,728 字节） |
| 父版本 | `661393da…`（stable-features-20261105） |
| 配置 | 与稳定版相同（`88612096…`） |
| 烧录 | `sftool -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after no_reset write_flash --verify firmware.bin@0x12010000`，再 RTS 复位 |
| 回退 | 稳定版 `artifacts/stable-features-20261105/firmware.bin`，同一命令 |

开机自检（烧录后串口）：CO5300 屏识别、`/dev/input0` 存在、BLE 广播、历史读到第 18 条、NVIC IPR[63]=IPR[77]=0x80。

**用户已确认**（在 `d43b3f92` 上，与当前版仅差第 4 节第 7 项）：晃动后扣下能触发安全确认，提示音持续响到点按钮，结束骑行→复盘→完成多次不卡，安全提示音能听到。
**未逐项确认**：侧键亮屏/进出列表/列表滑动/抬腕/设置页（用户说“差不多了”）；当前版唤醒遮罩修复未手测；5 次复位检查按用户要求跳过。

## 2. 本轮查明的问题（都有串口证据）

日志：`artifacts/safety-fixes-20261105/watch-serial-20261105.log`。

1. **摔倒确认从不弹出**：静止判定要求 |gx|+|gy|+|gz| < 5000 mdps，但这块表的陀螺仪零偏三轴合计约 4900–5300 mdps（零偏约 `[1180,-3660,-300]`），静置也判为“运动”。
2. **长时间晃动后不报警**：10 秒静止窗口从第一下撞击开始算，窗口内的后续撞击被忽略；晃动超过约 7 秒时窗口过期，只剩 15 秒的“持续静止”提示（20:35–20:38 日志）。
3. **结束骑行/复盘时整机卡死**：`drv_lcd.c` 中 `draw_core timeout` 后断言停机。
   根因：屏幕驱动把 LCDC1、EXTDMA 中断设为 NVIC 0xC0，其余中断都是 0x80（在手表上用 `xd 0xe000e400 128` 读回）。
   NuttX `arm_doirq()` 不支持受管中断之间嵌套，0x80 中断抢占 LCD/DMA 中断并在其中切换任务后，LCD 中断被“挂起”。
   两次崩溃现场分别是 `active=1, pending=0`（处理函数被挂起）和 `active=0, pending=1`（同优先级 EXTDMA 挂起导致 LCD 中断排队）。三次都发生在结束骑行保存后 2–3 秒。
   11-04 的 `draw_core timeout` 滑动卡死很可能是同一原因，但未单独复现。
4. **安全确认无声、熄屏不亮**；自动亮屏后第一次点按被唤醒遮罩吃掉。

## 3. 构建

- 树：WSL `Ubuntu-22.04`（不是默认的 20.04）`/home/wyj/velaride_history_cache`。
- 脚本：`artifacts/safety-fixes-20261105/wsl_build.sh`（从 `tools/` 运行；Git Bash 下需 `MSYS_NO_PATHCONV=1`）。
  依次：解开 activity-stage 应用源码 → 8 个补丁 → 编译 → 核对配置 → 把 `drv_lcd.c`、`drv_ext_dma.c` 恢复为 `velaride_safety_stage` 原版。
- 补丁副本在 `patches/`，构建用的厂商文件在 `drv_lcd.c`、`drv_ext_dma.c`，应用源码在 `app-source.tar.gz`。
- 本轮中间版本都在 `backup/`：`still-gyro-v2-9c8ac990`、`lcd-eof-67952343`、`safety-cue-99d4a8b3`、`safety-sound-3ddf0e6c`、`irqprio-f54aa1dc`、`impact-window-d43b3f92`、`wake-shield-57e472ce`。

## 4. 固件改动清单（相对 661393da）

| # | 补丁 | 改动 |
|---|---|---|
| 1 | `patch_still_gyro.py` | 预热时学习陀螺仪零偏，之后只在加速度平稳时慢速跟踪；静止判定用扣除零偏后的值。`velaride ride-status` 增加 `accel_delta/gyro_raw/gyro_sum/gyro_bias` |
| 2 | `patch_impact_window.py` | 每次撞击都重新开始 10 秒窗口；撞击次数仍每次摔倒记一次 |
| 3 | `patch_safety_sound.py` | 新提示音 `VR_SOUND_SAFETY`：闹钟音色，不受勿扰/静音/铃声开关限制，音量不低于 70%，优先级高于闹钟；shell `velaride sound play safety` |
| 4 | `patch_safety_alert_cue.py` | 安全确认出现时亮屏（同时撤唤醒遮罩），提示音循环到点按钮或结束骑行；按侧键本次静音 |
| 5 | `patch_lcd_timeout_eof.py` | 刷屏超时后若硬件确已完成（EOF、空闲、无错误），补发完成回调而不是断言；日志 `LCDC_EOF_RECOVERED` |
| 6 | `patch_lcd_irq_priority.py` | LCDC1、EXTDMA 中断改为 NuttX 默认 0x80（HAL 优先级 6→4） |

第 5、6 项改动了共享显示驱动，属于 AGENTS.md 要求说明理由并回归的范围；理由见第 2 节第 3 条。

## 5. 摔倒检测怎么工作（答辩可讲）

4 Hz 读 LSM6DSL：相邻两次三轴加速度差之和 ≥ 650 mg 记为撞击 → 最后一次撞击后 10 秒内连续静止 3 秒 → 手表亮屏、持续响铃、弹“安全确认”（我没事 / 暂停休息 / 需要帮助），30 秒倒计时 → 未应答或选“需要帮助”时，手机 App（已连接时）响铃倒计时，然后发定位短信并拨打第一位紧急联系人。
- 暂停中不检测；“暂停休息”和“需要帮助”会暂停骑行。
- 演示时用力甩一下再扣下松手，约 4–9 秒触发；轻敲不会触发（采样率只有 4 Hz）。
- 点屏幕的力度有时也会记一次撞击，只影响计数。

## 6. 仍存在的问题

- 用户反映“按键有时点一下没反应”：位置（哪个页面、触摸还是侧键）和是否在 661393da 上也有，都未查明。第 4 节第 4 项只修了熄屏自动亮屏这一种情况。侧键有 0.4 秒防连按（原有设计）。
- IRQ58（LCPU2HCPU，BT 邮箱）0x60、IRQ56（DMAC1_CH7）0x00 仍高于默认，由 BT/ROM 代码设置，未改。
- 手机重启强断 BLE 后，手表曾停止广播（19:16），新版未复现。
- 复位后手表时钟回退（RTC 不保持），需手机连接校时。
- TF 卡挂载仍未解决（与本轮无关）。
- 未做真实户外骑行，颠簸路面误报率未知。

## 7. 手机 App 现状

- **红米 K100 Pro Max（HyperOS，Android 16）已装 0.3.0**（`cb49ffaf…`；稳定版交接写的 `7B42548A…` 有误）。
  已实测：所有权限已授予；联系人 1 位已保存；“呼救演练”15 秒倒计时、闹钟铃声、震动、不发短信通过；“测试短信”系统回执成功 1 条（对方是否收到未确认）。
  手表联动的真实摔倒→手机倒计时→短信+拨号**未测**：当时手表检测有 bug，且之后手机拔了。
- **0.3.1 已于 20:54 覆盖安装**（`artifacts/safety-fixes-20261105/VelaRide-0.3.1-debug.apk`，`3CA7C5CB…`，versionCode 14），主机测试全部通过。
  实测：连接后“数据通道就绪·时间已同步”；串口 RTS 复位手表后 8 秒内自动重连并校时（20:55:37 断 → 20:55:45 连）。
  20:57 用户做了一次真实摔倒：手表 20:57:33 撞击 → 20:57:43 弹确认+亮屏 → 每 10 s 响铃 → 20:59:01 结束骑行（seq 19，不卡）。
  用户确认手机响铃倒计时、短信和拨号完成；手机端日志未读取（手机已拔）。其余联动项（手表“需要帮助”、迟到“我没事”、断电重连）延后。改动：
  - `WatchConnectionService`：BLE 断开后后台自动重连（3 s 起，指数退避到 30 s），通知栏显示“摔倒保护暂停，正在自动重连”；
  - `FallAlert.reset()` 重连后保留求助基线，断线期间按的“需要帮助”或已超时的确认在重连后仍会报警（`FallAlertTest` 新增 4 例）；
  - `PhoneLinkSession.reconnect()/holdReconnect()`，手动搜索时暂停自动重连 30 s。
- adb：`D:/desktop/VelaRide_AI/.local-tools/android-sdk/platform-tools/adb.exe`；手机已开“USB 调试（安全设置）”，重启手机后模拟点击才生效。Git Bash 下 `adb shell` 路径需 `MSYS_NO_PATHCONV=1`。
- 编译：`mobile/android/build.ps1 -SdkRoot <VelaRide_AI>/.local-tools/android-sdk -JavaHome E:/AppDownload/Android_Studio/jbr`。

## 8. 下一步

1. 插手机 → `adb install -r` 0.3.1（覆盖安装，不卸载）→ 连接手表。
2. 联动实测（会真的发短信并拨号，先告知联系人）：手表开始骑行 → 甩后扣下不点 → 30 秒后手机倒计时 → 短信+拨号；再测手表“需要帮助”、迟到“我没事”自动取消、手表断电重连后自动恢复。
3. 手机端增强（只改手机）：骑行实时看板（`ride_status`）、寻车（结束时记停车点，`nav_hint` 显示方向/距离）。
4. 答辩材料：本轮三个根因（陀螺零偏、撞击窗口、中断优先级嵌套）适合作为“底层适配”案例。
5. 本轮所有改动**均未提交 git**，提交前先核对 `git status`（还有 0.3.0 阶段未提交的改动）。

## 9. 固件回归检查（任何固件改动后）

串口：`artifacts/safety-fixes-20261105/serial_logger.py COM5 <log> <cmdfile>` 常驻录制（向 cmdfile 追加命令行即发送，`!reset` 为 RTS 复位），卡死时不要断电，先读日志。
手感：黑屏侧键亮屏、侧键进出列表、点列表项滑动、抬腕亮屏、设置页进出、结束骑行→复盘→完成。
