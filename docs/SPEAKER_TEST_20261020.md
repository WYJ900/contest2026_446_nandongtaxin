# 黄山派扬声器测试

> 2026-10-25 当前已接受的输出基线为 **v13 / `musicusb` / +6 dB 档**。
> 已按用户要求停止试听，后续默认只播一段；连续播放须得到明确要求。
> 最新参数、证据和复现方法见文末“2026-10-25 已接受的发声方案”。
> 下方 2026-10-20 的版本、音量范围和测试状态属于历史记录。

2026-10-20：在显示迁移版 rc17 上增加独立 NSH 命令 `speaker_test`。

## 使用

```text
speaker_test
speaker_test 1 0 pcm
speaker_test 1 0 music
speaker_test 1 0 music24
speaker_test 1 -24 internal
speaker_test 1 0 pa-only
speaker_test 1 0 silence
```

可选参数为重复组数（1–5）、数字增益（-24–0 dB）、模式。
默认播放一组 500、750、1000 Hz 短音，默认 -12 dB；
指定 `0 pcm` 使用用户确认可听清的较高音量。
每个音约 0.7 秒，另有淡出和间隔。命令结束关闭 PA42 控制的功放。
`music` 为约 16 秒原创电子琴旋律与伴奏，16 kHz/16-bit/mono；
`internal` 使用芯片内置 1 kHz 音，固定 -24 dB、约 1.5 秒。
`pa-only` 只打开功放 5 秒，不启用 DAC；
`silence` 连续输出零 PCM 约 5 秒，用于听测底噪。

Windows 工作区根目录可运行：

```powershell
.\tools\speaker_test.ps1
.\tools\speaker_test.ps1 -Port COM5 -Repeat 3
.\tools\speaker_test.ps1 -Mode music
```

串口为 COM5、1000000 baud。命令通过串口执行，不复位设备。
日志中的 `DMA advanced` 只证明传输计数发生变化，实际声音须听测确认。

## 硬件依据

工作区 `hardware-model/huangshan_pi/references/huangshan_v1_2.epro`：

- 原理图音频页：`SHEET/bfbe9dd79a064614aebc68082ceec943/31.esch`。
- U6 为 NS4150B，输入为 DAC1P/N，CTRL 接 PA42。
- CN2：1 脚为 SPK_OUTN，2 脚为 SPK_OUTP。
- 芯片正面朝上、USB 在左、天线在上时，SPK 上针为负，下针为正。
- [NS4150B 厂商数据手册](https://datasheet.lcsc.com/lcsc/Shenzhen-Nsiway-Tech-NS4150_C189961.pdf)：CTRL 高电平使能，典型启动时间 120 ms；程序等待 150 ms。

采用现有 SiFli HAL 的 16 kHz XTAL 时钟配置及 DAC1 初始化。
16 位单声道正弦峰值为 6000，DAC 数字增益为 -24–0 dB，含音量渐入渐出。
PCM 缓冲为 1024 字节、32 字节对齐，链接地址 `0x20045aa0`（SRAM）。
DMA 使用 HAL 动态分配及非中断传输，检查传输错误和计数停滞。
每音结束 Abort 释放通道后，不再 DeInit 已释放的通道。
本命令为板级诊断，尚不是通用 NuttX audio lower-half 或音乐播放器。
音乐使用独立 8192 字节 SRAM 循环缓冲，两个半缓冲各容纳 128 ms；
只填充已经播放完的半缓冲。监测 DMA 错误、DAC 欠载和调度落后。
音乐数据由代码合成，不需要把大块 WAV 加入固件。

## 源码与构建

当前有效源码在迁移树，未覆盖旧 U7 主树：

- `build/migrate/vendor_sifli/boards/sf32lb52/lckfb_huangshan_pi/src/speaker_test.c`
- 同目录 `CMakeLists.txt`：随 `CONFIG_EXAMPLES_VELARIDE` 注册命令。
- `tools/speaker_test.ps1`：Windows 有界串口调用入口。

```powershell
wsl -d Ubuntu-22.04 -- bash /mnt/d/desktop/VelaRide_AI/build/migrate/inc.sh speaker_format_20261020_v7 vendor_sifli/boards/sf32lb52/lckfb_huangshan_pi/src/speaker_test.c vendor_sifli/boards/sf32lb52/lckfb_huangshan_pi/src/CMakeLists.txt
```

当前安装产物 `build/migrate/out/speaker_format_20261020_v7.bin`：

- 9,587,000 字节；距配置分区边界余量 440,008 字节。
- SHA-256：`0b720acfdd018a4a7ab25cbfef4b1c8725b9f4175e578e487af683305c4f6ff1`。
- 静态 SRAM 496,860 / 523,008 字节，余量 26,148 字节。
- 构建通过。链接器仍有原工程的 RWX LOAD segment 警告。
- checkpatch 已运行；剩余为 SiFli HAL 自带的混合大小写 API/结构字段及工作区路径识别提示，未宣称零告警。
- 检查了参数上限、并发互斥、音频占用拒绝、DMA 缓存清理、错误退出和功放关闭。

## 备份和回退

写入前完整读回 `[0x12010000, 0x129a0000)` 到
`backup/speaker-20261020/installed-app.bin`（10,027,008 字节），
与 rc17 镜像前 9,580,896 字节逐字节一致。
另存 `backup/speaker-20261020/rc17-before-speaker.bin`。
写入不触及从 `0x129a0000` 开始的配置区。

回退：

```powershell
.\sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "backup/speaker-20261020/rc17-before-speaker.bin@0x12010000"
.\tools\reset.ps1
```

## 实机验证

v2 已烧录，启动和两次命令执行正常；三音均打印 `DMA advanced`，
`speaker_test 0` 被拒绝，结束后主应用和 BLE 任务仍在。
证据：`backup/speaker-20261020/speaker-first.log`、`speaker-repeat.log`。

**v2 用户听测未通过：只有咔哒、滋滋声，听不出三声提示音。**
不能把 DMA 首次发生变化视为完整播放通过。
v3 增加连续 DMA 进度、估算采样率和 DAC 寄存器诊断，
并将数字 DAC 使能推迟到 DMA 已准备好以后。
实测每音 120 次连续计数变化，约 16 kHz；提高到 0 dB 后用户仍报告卡卡声。

v4 关闭硬件慢速/过零音量调整，保留软件淡入淡出；
每音前重置数字通路，每音后关闭数字 DAC，再释放 DMA，避免间隙欠载。
日志 `loop-v4.log` 显示约 16 kHz，三个音的 DAC IRQ 状态均为 0，
DMA 源地址/目的地址和 PCM 数据正确。
用户在 0 dB 循环听测后明确确认“能听到清楚的三种音调”。
这证明该配置在此板上可发出清晰音调，不能单凭本次测试区分
启动顺序、音量调整和主观听测时长各自贡献，也未量化频响/失真。

循环通过 `/data/spk.sh` 后台执行，以 `/data/spk.go` 为运行标记；
删除该标记会在当组三音结束后停止。用户要求音乐后已删除标记并确认退出。

v5 增加原创音乐。使用同一份 C 合成函数在主机生成 256,000 个采样，
开启 UBSan，无报错；范围 -8759–8777、RMS 1957.76，无削波。
参考 WAV：`backup/speaker-20261020/speaker_music_reference.wav`。
音乐实机连续播放已完成：

- 0 dB：257,662 个采样、125 次半缓冲填充，返回 0；
  用户能听出旋律，但反馈明显破音或沙沙声。
- -6 dB 对照：257,612 个采样、125 次半缓冲填充，返回 0；
  用户反馈“只是声音变小，杂音差不多”，未达到音质验收。
- 两次均未触发 DMA 错误、DAC 欠载或 >100 ms 调度落后检查。
  播放结束关闭功放。日志为 `music-v5.log`、`music-v5-minus6.log`。
- 无数字削波、无检测到的传输中断，不等于已排除模拟通路、
  功放、喇叭或接线造成的失真；尚不能评价板载麦克风。
- 当前没有循环播放，功放已关闭。供用户用电脑对照同一份参考 WAV，
  区分合成音色本身与板端额外噪声；尚未进行电气波形或录音测量。

用户随后确认参考样音基本正常，板端发糊且沙沙声明显；喇叭规格为 8Ω/1W。
v6 增加静音对照。已执行 PA-only 5 秒，以及零 PCM 5 秒：
后者传输 81,614 个采样、39 次半缓冲填充，返回 0；结束关闭功放。
日志为 `pa-only-v6.log`、`silence-v6.log`。
用户确认零 PCM 测试“中间安静，啥也没有几乎”，说明当次空载音频状态
没有明显持续底噪；不据此排除信号相关的模拟失真或负载问题。

v7 增加 `music24` 对照：同一份 16 位源采样乘以 256、保留低 24 位，
每个 DMA word 一个采样，切换 DAC `DATA_FORMAT=0`；
保持同样的曲子、16 kHz 采样率及增益。遍历全部 65,536 个
16 位值验证符号与幅值能无损还原。
半缓冲为 64 ms，调度超时阈值相应收紧到 50 ms。
已实机播放完 24 位对照：257,616 个采样、251 次半缓冲填充，
返回 0，播放结束关闭功放。日志 `music24-v7.log`。
用户反馈“几乎一样，仍然发糊、沙沙声重”，24 位路径未改善听感。
当前不能确认是喇叭本体、板级模拟输出/功放、连接还是尚未发现的配置问题。
下一步需用另一只已知正常的 8Ω 喇叭对比，或测量 DAC/SPK 差分输出；
没有继续盲目改变模拟电源或增益寄存器。

最新状态：用户要求重播并再次确认“杂音很严重，可以听到音乐”。
两遍音乐均完成且返回 0，日志 `music-replay-two.log`。
随后沿用用户此前的连续播放要求，启动 0 dB 的音乐循环，方便录音：
后台 `sh /data/spk.sh`（当次 PID 36），运行标记为 `/data/spk.go`。
以上为 2026-10-20 的历史循环，现已结束。旧脚本的停止方法为
`rm /data/spk.go`，当前一段音乐播放结束后循环退出、功放关闭。
日志 `start-music-loop.log`；等待用户提供实际音频/视频以分析杂音形态。

## 2026-10-25 已接受的发声方案

### 当前状态和用户要求

- 本节验收时设备安装 **v13**。循环已按用户要求结束，功放关闭，蓝牙广播恢复。
- 后续只有用户明确要求试听时才播放。脚本默认 `--loops 1`，约一段
  16 秒的测试；持续循环需要明确要求并显式使用 `--loops 0`。
- 开机脚本没有加入扬声器自动播放。
- 用户确认：“音量够，tatata 明显减轻或没有了”。

### 已验证的硬件和参数

- 扬声器标记 1508/1507，**8Ω、1W**，两线无源扬声器。
- 本次供电为**仅连接电脑 USB，未接电池**。
- CN2：红线接 `SPK_OUTP`（2 脚），黑线接 `SPK_OUTN`（1 脚）。
  两侧固定脚属于 `SPK_GND`；扬声器的两根线均接功放输出信号脚。
- 运行模式：**`musicusb`，+6 dB 档，16 kHz、24-bit、单声道**。
- 采用预先生成的四秒音乐缓冲，DMA 循环读取，播放期间不重写缓冲。
  每组持续约 16 秒；这仍是板级固定片段测试。
- 音频 DMA 源显式调用 NuttX `up_clean_dcache()`，不依赖 SDK 仅识别
  PSRAM 的缓存地址判断；录音和语音流接入另行验证。
- USB 模式的软件预增益、软压缩和直流阻断在播放前完成，DAC 硬件增益
  保持 0 dB。命令的“+6 dB 档”对应处理目标，不等于直接给 DAC 加 6 dB。
- 当前四秒段的数字 RMS 与原始 +6 dB 相差 **-0.081 dB**；峰值从约
  **17455** 压至 **5999**（16-bit 等效值），约降低 9.3 dB。
  循环首尾为零，含 20 ms 淡入淡出。音乐动态和音色会受压缩影响。
- 关功放后等待 100 ms 再关模拟通路；主机每组之间另等待 200 ms。
- 播放脚本在 BLE 已就绪、未连接手机时暂停广播，结束后恢复原状态。
  已有手机连接时不会强行断开连接。

### 验证证据和边界

- 连续完成 **138 组**，至少 **36.8 分钟音频**，日志没有 `SFBL` 复位标记。
- 各组结束返回 `result=0`，最终功放关闭、恢复广播返回 0。
- 主机对实际 C 波形生成/打包代码运行 ASan/UBSan：16/24-bit 打包、
  DMA 缓冲边界、31 档增益、USB 模式峰值、RMS 和循环接缝检查通过。
- 记录与原始日志：
  - `backup/speaker-20261025/accepted-v13-validation.json`
  - `backup/speaker-20261025/accepted-usb-profile.json`
  - `backup/speaker-20261025/usb-v13-live.log`
  - `backup/speaker-20261025/host-tests-usb-v13.log`
- 上述为组合设置的实测结果，尚未单独量化缓存维护、固定缓冲、广播暂停
  各自对噪声的贡献；此前复位的电气根因仍未独立测量确认。
- 验证范围为当前固定测试片段和 USB 供电条件。实际声压/输出电功率、
  任意 TTS 语音、麦克风录音、手机连接和音频同时工作仍需另行验收。

### 固件和回退

- 已接受的镜像：`build/migrate/out/speaker_usb_20261025_v13.bin`
- 镜像大小：9,589,364 字节；未覆盖 `0x129a0000` 起的配置区。
- SHA-256：
  `893b0662f28bae22a3c2a0b94916dcfcd9866cc3eea5ecafc91a737a7954de73`
- 对应配置：`build/migrate/out/speaker_usb_20261025_v13.config`
- 留档的播放源码：`backup/speaker-20261025/speaker_test.accepted-v13.c`
- 实际控制脚本：`tools/speaker_loop.py`；预期停止时完成当前段，随后关闭
  功放并恢复蓝牙广播。

2026-10-26 已写入 `speaker_mic_20261025_v14.bin`，新增板载麦克风采集
和共享音频占用锁。5 秒录音导出并在电脑播放后，用户确认人声可听清。
v13 继续作为本节音乐输出的验收与回退基线；语音版本进展见
[板载语音收发记录](VOICE_BRINGUP_20261026.md)。

### 下次明确要求试听时的单段复现

在工作区根目录使用新日志名和新的停止标记路径：

```powershell
$audioStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
py -3 tools/speaker_loop.py --port COM5 --gain 6 --mode musicusb --quiet-radio --loops 1 --log "backup/speaker-20261025/check-$audioStamp.log" --stop-file "backup/speaker-20261025/check-$audioStamp.stop"
```

这条命令仅供后续按要求执行，本次记录过程没有再次启动播放。

### 后续语音助手方向

用户选择手机中转，先做较容易落地的安卓方案，再评估 iPhone。
首版按键录音、手机联网处理、接收回复后播报；已验证麦克风到电脑的
人声采集，板端语音文件播放及端到端对话仍待验收。当前广播暂停策略需要针对已连接手机的
音频传输重新验证，不能直接作为连续双向语音的保证。
