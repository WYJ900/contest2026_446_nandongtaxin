# 功能扩展接续：基础回归与安卓兼容

日期：2026-10-27。接续 [扩展规划](EXPANSION_ROADMAP_20261027.md) 和
[音频阶段交接](HANDOFF_AUDIO_STAGE_20261027.md)。

> 后续已按用户要求转入手表本地工具开发，当前镜像与最新状态见
> [倒计时阶段交接](HANDOFF_WATCH_TOOLS_20261027.md)。本文保存前一轮状态。

## 当前范围

手表固件未刷写、源码未改动，仍为放音候选 `23ea7b74e6f6aa4b…`，
9,592,552 字节。归档固件与 WSL 构建产物 SHA-256 一致。本轮没有录音或放音。
用户对“滑动、菜单、抬腕、听感及断电冷启动”询问回复“ok的”，原话记录在本轮证据中；
没有独立的冷启动串口记录，不将 RTS 复位描述为完全断电测试。

当前变更限于电脑上的验收工具和安卓 App；原有固件归档保持不变。
自动检查已完成：5 次 RTS 复位全部通过，随后连续静置 1,800.04 秒、0 次重启；
静置后触摸节点、UI Task、唤醒诊断及旧 BLE 均通过。两轮新版基础握手、
校时和记录读取也通过，第 11 条记录与测试前逐字段相同。
汇总：[验证摘要](validation-expansion-20261027/summary.json)。

下一步先在安卓真机验证连接、校时、同步第 11 条记录，再拆分移植 GPS 扩展。
不刷回历史 v18/v19，不引入无触摸时继续启动表盘的降级处理。

## 安卓 0.1.1

- 原 0.1.0 在握手中强制要求 `phone_clear` 成功，当前基线只会返回 `unknown_cmd`，
  因此自动校时和骑行同步会被中断。
- 新增 `WatchHandshake`：先 ping；尝试清理旧定位。仅在设备明确返回 `unknown_cmd`
  时关闭 GPS 扩展，继续基础同步。其他设备错误及通信错误继续报告失败。
- 旧固件下定位按钮不可点击，并说明等待固件升级；新固件的清理流程保留。
- 版本号 0.1.1 / versionCode 2，保留原 0.1.0 APK，使用原开发签名。
- APK：工作区 `exports/mobile/VelaRide-0.1.1-debug.apk`。
  SHA-256 `ceb52dabb7246fbd45241211fa7e42f2b06a9debf1521446b1b029f5a3e7251b`。

Java 协议与新旧握手测试、APK 构建和 v2/v3 签名核验通过。
用户确认“暂时没有安卓手机，先完成自动检查”，本轮暂缓手机实测。
`adb devices -l` 没有手机。APK 的真实安装、权限流程、Android BLE 回调及
实际 GPS 尚未验证；电脑 BLE 检查只能证明手表协议行为。

## 回归工具 E3

实现统一放在团队仓 `scripts/boot_check.py`，工作区 `tools/boot_check.py` 保留转发入口与
原 `--cycles 5 --log ...` 接口，新增：

```powershell
py -3 -X utf8 tools/boot_check.py --cycles 5 --idle-seconds 1800 --ble --log backup/expansion-20261027/04-baseline-reset-idle-ble.json
```

- 触摸节点必须出现在命令输出中，不能只匹配命令回显。
- 要求 UI Task 存在；`velaride_ble` 或同名 pthread 不代替 UI 主任务。
- 触摸报错、唤醒诊断错误、异常 idle 值、未完成全部复位都不能通过。
- 串口打开失败、异常或用户中断会保存失败 JSON，返回非零退出码。
- 静置期间只接收串口，不发送诊断命令；结束后重新检查触摸节点、UI 任务及唤醒状态。
- `--ble` 检查旧 ping/status/latest 响应和事件名；不写骑行记录或备注。
- `--check-tf` 要求 `mount` 显示 `/data/tf type vfat`，空目录不能通过。
  当前无卡挂载，本轮未启用该选项；尚未做 TF 读写、拔卡或供电尖峰验收。
- 自动通过不代表滑动、菜单、抬腕实物行为或听感通过。

主机测试：`py -3 -X utf8 -m unittest discover -s my-contest-repo/tests/boot_check -v`，10 项通过，
含历史五轮串口日志回放及失败路径。

## 证据与归档

本轮证据目录：工作区 `backup/expansion-20261027/`。

| 文件 | 内容 |
| --- | --- |
| `01-initial-status.log` | 初始串口状态、休眠和导航统计 |
| `02-legacy-ble.json` | 旧 BLE ping/status/latest，第 11 条记录可读 |
| `03-health-format.log` | 触摸节点、进程和真实挂载列表；无 TF 挂载 |
| `04-baseline-reset-idle-ble.json` | 五轮复位、30 分钟静置和旧 BLE 合并报告；以文件最终 passed 为准 |
| `05-android-build.log` | Java 测试、APK 构建、版本和签名核验 |
| `07-archive-configure.log`、`08-archive-build.log` | 独立目录从源码归档完整构建成功，配置一致 |
| `09-size-comparison.json` | 普通独立重建比基线多 192 字节，来自厂商驱动的 8 条绝对路径 |
| `10-reproducibility.json` | 控制路径和日期后的逐字节复现尝试；当前仍为 false，不能当作同一镜像 |
| `11-companion-basic.json` | 两轮 ping → phone_clear 不支持 → 校时 → 状态 → 记录读取通过 |
| `12-after-companion.log` | BLE 测试后的实际日期、触摸节点、UI Task、唤醒诊断均正常 |
| `before/` | 修改前的回归工具、安卓源码与原 APK |
| `speaker-source-snapshot/` | 当前 WSL 源码归档、逐文件哈希及选定构建产物，归档校验状态以 manifest 为准 |
| `generated-interfaces.tar.gz` | 原构建目录内 48 个 JIDL 生成文件的补充归档，有独立哈希清单 |
| `android-0.1.1-source.zip`、`android-0.1.1.patch` | 本轮安卓源码快照和限于兼容修复的补丁 |

源码归档排除 `.git` 和整个 `cmake_out`，另存固件、ELF、配置、CMakeCache 和 build.ninja。
202,311 个文件/链接已逐项校验，3,896,767,573 字节源文件，压缩包 1,690,753,233 字节。
清单未发现指向树外的软链接；工具链仍为本机外部依赖，没有打包。

独立构建目录 `/home/wyj/velaride_speaker_archive_check_20261027` 已完整编译成功，
配置逐字节一致。普通重建固件为 9,592,744 字节，哈希 `80322ee5…`。
通过 `codesize` 定位，多出的 192 字节恰为 8 条源码路径每条增长 24 字节；
函数/变量符号大小未变，另有两处编译时间变化。

控制路径和编译时间后，镜像大小恢复 9,592,552 字节，但哈希仍不同。
剩余差异集中在 JIDL 生成的接口声明/表项排列；构建每次运行生成器，
原生成文件副本也会再次被生成输出覆盖。本轮未修复生成器或其构建依赖。
**没有宣称逐字节重现成功，也没有刷写任何重建实验镜像。**
精确可回退的镜像仍为原 `23ea7b74…`。

本轮归档是后续统一源码的保存基础，E1“团队仓统一并打标签”以及确定性生成仍未完成。

## 待完成

1. 安卓真机安装与基本交互验收（用户暂时没有手机，本轮明确暂缓）。
2. 新 GPS 生命周期和连接页按小补丁迁移，每次继续基础回归。
3. TF 卡阶段需 FAT32 卡就绪，再做最小挂载/读写/拔卡测试。
4. 单独处理 JIDL 的确定性生成和依赖顺序，再验证固件逐字节复现。
