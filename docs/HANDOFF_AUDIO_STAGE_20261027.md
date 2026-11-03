# VelaRide 基础恢复、麦克风与放音阶段交接

记录日期：2026-10-27，时区 Asia/Shanghai。本文接续
[10-26 回归交接](HANDOFF_REGRESSION_20261026.md)，是当前最新状态。
扩展方向另见 [功能扩展规划](EXPANSION_ROADMAP_20261027.md)。

## 1. 交接时的真实状态

- 手表上的固件：**麦克风 + 放音阶段候选**
  `my-contest-repo/artifacts/mic-speaker-stage-20261027/firmware.bin`，
  9,592,552 字节，SHA-256
  `23ea7b74e6f6aa4bbe9c2e9e925b034eed1ef1cf5a37fa255f2f105222068941`。
  `uname` 仍显示 `Oct 27 2026 13:09:03`，原因是增量编译没有更新版本时间戳，以哈希为准。
- 10-26 的触摸失联和静置黑屏（连续 `SFBL`）：**用户手工修好屏幕端排线后，至今没有复发。**
- 按 AGENTS.md 的顺序：阶段 1（恢复基础）已通过，阶段 2（麦克风）已通过并冻结，
  放音已加入并通过自动测试和一次实物播放，用户听感待最终确认。
  阶段 3（新蓝牙/手机 App）尚未开始。
- 第 11 条骑行记录全程保留。没有擦除配置区，没有持续放音。

## 2. 10-26 故障的结论

| 证据 | 说明 |
| --- | --- |
| 原样 rc17、原样 U7（都曾验收）在 10-26 同样失联 | 不是新代码引入的回归 |
| 故障时屏幕侧把 SCL 拉低，GPIO 复位触摸芯片无效；仅拔插屏幕端排线能恢复 | 指向屏幕连接 |
| 官方板级说明 `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/README_zh-cn.md:155`：只打印 `SFBL` 反复重启 = AMOLED 拉低 USB 供电 | 与 10-26 静置黑屏日志吻合 |
| 修好排线后 4 版固件 × 5 次复位 = 20 次全部正常，中间录音 6 次、放音 3 次 | 修复后行为稳定 |

**结论：主因是屏幕端排线连接。** 具体触点、是否掉电没有用仪器测量，SFBL 与供电的关系属于推断。

拖延的原因属于流程：故障与音频开发时间重合，被误判为软件冲突；v19“无触摸也启动表盘”
掩盖了故障；每版只验证一次启动就当作已恢复。今后每一步都用第 5 节的复位测试加用户上手确认。

**排线以后仍可能松动**（装壳、受压、弯折）。再出现触摸失灵或连续 SFBL，先查排线，再查软件。

## 3. 阶段链与证据

证据目录：`backup/stage1-20261027/`（编号即时间顺序）。

| 固件 | SHA-256 前 16 位 | 相对上一版的变化 | 自动复位 ×5 | 实物 |
| --- | --- | --- | --- | --- |
| 原样 rc17 | `b04d766146ac6c9b` | — | 通过（04） | 10-26 用户确认过 |
| 重建 rc17 | `f1226911281ba6fe` | 源码重编，功能不变 | 通过（06） | 未单独上手 |
| 麦克风阶段（**已冻结**） | `31610eb17fe7ecbe` | 新增 `mic_test` | 通过（08） | 用户：滑动、抬腕正常，7 次滑动 51.9 Hz；录音能听清 |
| 放音候选，无自动放大 | `25627d9eb043a19f` | 新增 v16 `speaker_test`，改用共享锁 | 通过（19） | 放音 2 次 `result=0`；用户：响了但声音小 |
| **放音候选，当前** | `23ea7b74e6f6aa4b` | 轻录音自动放大，最多 ×4 | 通过（26） | 放音 1 次 `result=0`，放大倍数 1（输入够大）；听感待确认 |

冻结目录（固件和配置只读，附 `manifest.json`、阶段源码、构建脚本与证据）：

- `my-contest-repo/artifacts/mic-stage-20261027/`
- `my-contest-repo/artifacts/mic-speaker-stage-20261027/`
- 无自动放大的版本保留在 `backup/stage1-20261027/mic-speaker-stage-v16dsp.bin`

三版配置文件逐字节相同，只有板级 CMake 增加了命令注册。

## 4. 本轮代码改动

只新增板级诊断命令，**没有改动原应用、驱动、启动、时钟、引脚、显示、触摸或电源代码。**

| 文件 | 内容 |
| --- | --- |
| `mic_test.c`、`audio_test.h`、`audio_test_lock.c` | 麦克风阶段（10-26 已写好，本轮首次验收）：16 kHz/16-bit 单声道，1–5 秒，丢弃前 200 ms，存 `/data/mic_test.pcm`，`mic_test dump` 带校验导出 |
| `speaker_test.c` | v16 原文件（10-26 13:17）。删除自带的锁，改用共享锁，录音和放音不会同时运行。`mic` 模式日志增加 `boost=` |
| `voice_test_dsp.h` | 新增 `voice_test_boost()`：原始峰值小于 500 时按整数倍放大到接近 1000，最多 ×4（+12 dB）。之后仍走 v16 的高通、软限幅和 ±5999 上限 |
| 板级 `CMakeLists.txt` | 末尾追加 `mic_test`、`speaker_test` 两个应用的注册 |

自动放大的主机测试（ASan/UBSan，`24-voice-boost-host-test.log`）：

- v16 验收录音放大倍数为 1，输出 RMS 1066，与验收时一致；
- 今天两段偏轻的录音放大 ×4，RMS 1017 和 871；
- 边界值 0、249/250/251、500/501、±32767、−32768、空输入全部通过。

板上实测只走了放大倍数为 1 的路径，×4 路径只在主机上验证过。

## 5. 复现与验证

构建（WSL Ubuntu-22.04，Git Bash 下必须加 `MSYS_NO_PATHCONV=1`）：

```bash
MSYS_NO_PATHCONV=1 wsl -d Ubuntu-22.04 -- bash /mnt/d/desktop/VelaRide_AI/tools/build_speaker_stage.sh
```

- 构建树 `/home/wyj/velaride_speaker_stage`，由已核对的 `velaride_microphone_stage` 复制而来，不含残留的 `touch_probe.c`。
- 脚本输出相对麦克风阶段的差异清单，并与冻结版配置逐字节比较。

刷写（只写应用区）：

```bash
./sftool.exe -q -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset \
  --connect-attempts 2 write_flash --verify '<firmware.bin>@0x12010000'
```

每次改动后必须跑的基础检查：

```bash
py -3 -X utf8 tools/boot_check.py --cycles 5 --log backup/<目录>/<名>.json
```

- 每轮 RTS 复位后检查：只有 1 次 SFBL、没有触摸报错、`/dev/input0` 存在、`velaride` 进程在运行、`wake-status` 正常。
- 之后请用户滑动、开菜单、抬腕，再读 `velaride nav-stats`，以 51–54 Hz 为参考。

录音和放音（单次，有上限）：

```bash
py -3 -X utf8 tools/mic_capture.py --seconds 5 --countdown 3 --keep-radio --output exports/voice/x.wav
py -3 -X utf8 tools/mic_capture.py --seconds 5 --countdown 3 --keep-radio --playback --gain 6 --output exports/voice/x.wav
```

本阶段固件没有 `velaride_ble adv-off` 等广播控制命令，所以必须加 `--keep-radio`，录音和放音都在广播开启时进行。

### 操作陷阱

- **`mic_test` 不带参数会直接录 3 秒；`speaker_test` 不带参数会直接放测试音。** 只能带明确参数调用。确认命令存在用 `speaker_test 1 1 1 1`，它只打印用法。
- `/data` 是内存盘（tmpfs）。录音文件一复位就消失，TF 卡未插也未挂载。骑行记录在 Flash 配置区，不受影响。
- 控制台偶尔会落到 ai_agent（提示符 `vela>`），发 Ctrl-D 回到 NSH。
- 增量编译不更新 `uname` 时间，版本以哈希为准。

## 6. 尚未覆盖

- 静置 30 分钟以上，以及完全断电后冷启动。本轮只做了 RTS 复位。
- 放音期间暂停 BLE 广播（v13 验收时的条件）。目前没有听到杂音反馈，需要时再评估是否改 `velaride_ble.c`。
- 自动放大 ×4 在板上的实际听感。
- 放音阶段候选的用户最终听感，以及放音后再次上手滑动的确认。
- 正式的 NuttX 音频设备（`/dev/audio/pcm0`）。现在只有诊断命令，界面还不能调用声音。

## 7. 下一步

1. 补齐第 6 节前两项，然后把放音候选正式冻结为阶段基线。
2. 阶段 3：新蓝牙/手机 App。原有 BLE 是需要保住的旧功能，新功能只在它的基础上增加。
   10-26 的手机代码（v17/v18：`phone_clear`、手机定位字段、连接页）保存在
   `backup/restore-20261026/feature-work-before-restore.zip`，应拆成小补丁逐个移植，每个补丁后都重跑第 5 节的检查。
   v19 的“无触摸也启动表盘”属于降级处理，**不移植**。
3. 安卓 App（`exports/mobile/VelaRide-0.1.0-debug.apk`）尚未在真机上安装验证。
