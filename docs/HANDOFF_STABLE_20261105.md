# 稳定功能版与手机联动交接（2026-11-05）

**已被 [摔倒检测修复交接](HANDOFF_SAFETY_20261105.md) 取代，新对话先读那页。** 决赛 11 月 7–8 日（每队 8 分钟演讲 + 2 分钟问答，建议真机演示）。下一步只做手机 App 联动，**手表固件不要再动**，除非 App 联动确实需要固件新命令，且改完必须重跑本页“回归检查”。

## 1. 手表当前固件（用户已确认正常）

| 项 | 值 |
|---|---|
| 文件 | `artifacts/stable-features-20261105/firmware.bin` |
| SHA-256 | `661393dadc5a1a3852b0e516e6f7d7e648883b7866ab9377d39bf6dd93308ad2`（9,802,792 字节） |
| 烧录 | `sftool -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after no_reset write_flash --verify firmware.bin@0x12010000`，再 RTS 复位 |
| 配置 | 与 `activity-stage-20261104/config` 相同（`88612096…`） |
| 用户确认 | 侧键亮屏/进出列表、点进列表项滑动均正常 |
| 自动检查 | 5 次 RTS 复位、触摸节点、抬腕状态、旧 BLE ping/ride_status/ride_latest 通过（`boot-five-ble.json`） |

包含：GPS 父版全部功能 + 安全确认 + 16 条历史/补传 + 复盘 + 休息/补水/停车建议/距离目标/分段 + GPX 路线提示 + 停车语音（需 TF，本次不插卡不演示）+ 久坐提醒。

**未在实物上验证**：真实骑行保存与手机补传、冲击后安全确认触发、50 分钟久坐提醒、GPX 室外、TF 语音。不能写成已验收。

手表历史当前只有第 11 条。第 12 条（11-04 的 13 秒测试骑行）在中途刷入 GPS 父版运行时被覆盖；
覆盖前的 4 MiB 配置区备份在 `backup/rollback-gps-20261105/before-config-region.bin`（`eaa0d890…`，非公开）。需要时整体写回 `0x129A0000`，但会同时回退手表设置。

## 2. 本轮查明的两个故障

按阶段固件逐一实测（均为原归档文件，未重编）：

| 固件 | 内容 | 实测 |
|---|---|---|
| `9041c538` | GPS 父版 | 正常 |
| `9308cce7` | + 安全确认 | 正常（用户确认） |
| `a0c45f3c` | + 16 条历史 | 列表点进去卡住 |
| `044ae8a2` / `8027be11` | 更后阶段 | 卡住，044ae8a2 卡住时串口也无响应 |
| `08cc1928` | 综合 + 修复 1 | 列表不卡，但侧键失灵，抬腕计数 sleeps/raises=492 |
| `661393da` | + 修复 2 | 用户确认正常 |

**修复 1：历史扫描放入内存缓存**（`patch_history_cache.py`）。16 条历史阶段起，每次 `load_latest` 都探测 17 个 KVDB 键；实机测得每个空键约 40 ms，一次约 0.6 s，而表盘、运动页、BLE `ride_status` 都会调用。改为开机扫描一次、之后读内存，任何一次保存后失效重扫。卡顿与该改动的相关性已由“缓存后不卡”支持，但更长串口无响应的完整机理未单独测量。

**修复 2：LSM6DSL 访问加锁**（`patch_sensor_lock.py`）。NuttX `lsm6dsl` 驱动无锁，一次读取是多次 I2C 寄存器访问。久坐提醒在设备线程每秒读取，抬腕检测在 UI 线程每 50 ms 读取，交错导致错乱样本、亮灭屏抖动，侧键唤醒被吞。现在所有读取经同一互斥锁。修后 30 秒观察无误唤醒。

**之前误判需更正**：11-04 归因于 TF 插卡、LCD 完成中断的结论不再作为主因。`display-completion-stage`（`4b207583`，LCD 补丁）不要使用，它让按键迟钝。TF 挂载仍是未解决的独立问题。

## 3. 构建方法（必须用干净树）

- WSL 树 `/home/wyj/velaride_history_cache`：由 `velaride_safety_stage` rsync，屏幕驱动为原版。
  **不要用 `velaride_product_features`**，那棵树带 LCD 补丁。
- 脚本：`artifacts/stable-features-20261105/wsl_build_history_cache.sh`（解开 activity-stage 应用源码 → 两个补丁 → 编译 → 核对配置）；
  测试：`wsl_test_history_cache.sh`（19 组 ride_session ASan/UBSan）。WSL 路径需 `~/.local/bin`（kconfiglib）。
- 注意：根目录 `my-contest-repo/app/velaride` 与实际烧录源码不一致，以各阶段 `app-source.tar.gz` 为准。
- 增量编译不更新 `uname` 时间，以固件哈希为准。全速 `read_flash` 大块读取会超时，读配置用 500000 波特 1 MiB 分块。

## 4. 手机 App 现状

`exports/mobile/VelaRide-0.3.0-debug.apk`（`cb49ffafa85440d8…`；原写 `7B42548A…` 有误，11-05 晚更正），**已编译、主机测试通过，未装到红米、未提交 git**。

新增“摔倒呼救”：
- `FallAlert.java`：根据 `ride_status` 的 `safety_pending/safety_remaining_s/safety_help/safety_last_action` 判断；连接时以当前计数为基线，旧求助不误报；手表上迟到的“我没事/暂停”或结束骑行会自动取消。
- `EmergencyPlan.java`：最多 3 位联系人解析、短信文本（高德 WGS84 链接、精度、定位时效）。
- `EmergencyCenter.java`：倒计时（未确认 15 s，主动求助 5 s）、警报铃声+震动、全屏通知、取当前定位或最近一次 GPS、群发短信（结果回执）、拨打第一位联系人（无权限则打开拨号盘），演练模式与测试短信。
- `EmergencyActivity.java`：锁屏全屏倒计时页。
- `WatchConnectionService`：后台每 2 s 读 `ride_status`；前台服务类型加 `location`。
- `MainActivity`：新增“摔倒呼救”卡片（开关、联系人、授权、锁屏全屏设置、演练、测试短信）。
- Manifest 新增 SEND_SMS、CALL_PHONE、VIBRATE、USE_FULL_SCREEN_INTENT、FOREGROUND_SERVICE_LOCATION。
- `tests/FallAlertTest.java` 已加入 `build.ps1`，版本 0.3.0（versionCode 13）。

风险/待查：
- 轮询与 UI 共用单 BLE worker；`BleLink.request` 超时会断开连接，后台长时间轮询的稳定性未实测。
- 红米 HyperOS 可能限制后台启动 Activity、短信需“始终允许”，后台定位可能被拒（会退回最近一次 GPS）。
- 手表若断开，倒计时继续，需用户在手机取消。

## 5. 下一步（新对话）

1. 安装 0.3.0 到红米（覆盖安装，不先卸载）：授权短信/电话/定位/通知/锁屏全屏，联系人填队友，先“演练”，再“测试短信”。
2. 实测摔倒链路：手表开始骑行 → 敲击后静止 → 不确认 → 手机倒计时 → 短信+拨号；再测手表点“需要帮助”及迟到确认取消。
3. App 联动增强（只改手机端）：骑行实时看板（`ride_status` 时长/速度/安全状态）、寻车（骑行结束记停车点，复用 `nav_hint` 在手表显示方向/距离）。
4. 每次改完：`mobile/android/build.ps1` 编译与主机测试；手表连接后重跑旧 BLE 检查。
5. 提交 git（本轮改动均未提交）、更新 `CURRENT_STATE.md`、准备答辩材料（“主动 + 执行”故事线、底层适配清单、Skills、商业模式）、录备用演示视频。

## 6. 回归检查（任何固件改动后）

`python my-contest-repo/scripts/boot_check.py --port COM5 --cycles 5 --ble --log <log>`，
再由用户上手确认：黑屏侧键亮屏、侧键进出列表、点列表项滑动、抬腕亮屏、设置页进出。
自动检查不能替代手感确认（本轮 `8027be11` 自动检查全过，但实物会卡）。
