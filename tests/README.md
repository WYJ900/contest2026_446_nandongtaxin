# 原生应用回归测试

## 安全确认

骑行状态测试现包含 14 组场景，验证确认/暂停/求助意向、一次超时、迟到确认、
暂停期间确认计时、未处理结束、计数上限、V1/V2 只读加载与 V3 保存。
`python3 tests/test_ble_safety_payload.py` 在 Linux/WSL 编译实际 BLE JSON 格式表达式，
检查最坏长度与 GPS/安全字段。UI 检查新增真实 LVGL 的侧键保护、二次确认、
按钮边界、倒计时与骑后处理结果；硬件检测准确性和真实操作仍需实测。

## 声音与实时音量

`bash tests/sound/run.sh` 验证声音服务、PCM 渲染和板级后端错误清理，启用 ASan/UBSan。
包含 0–100% 增益边界、实时静音/恢复不重启、合并保存、VRS1→VRS2 迁移和 CRC。
UI 测试加 `-DVELARIDE_SOUND=ON` 覆盖真实指针拖动音量滑块与加减按钮。
模拟硬件检查不能替代听感；本板实测见 `docs/HANDOFF_VOLUME_20261027.md`。

## 手表倒计时

`bash tests/countdown/run.sh` 运行独立状态机的 ASan/UBSan 检查。
界面测试配置时加 `-DVELARIDE_COUNTDOWN=ON`，启用真实 LVGL 按钮交互、
菜单路由、熄屏到时唤醒、勿扰和手势期间延迟横幅检查。
默认 OFF 继续验证旧界面；固件开关 `EXAMPLES_VELARIDE_COUNTDOWN` 默认关闭。

## 开机与静置验收工具

在团队仓目录运行：

```powershell
py -3 -X utf8 -m unittest discover -s tests/boot_check -v
py -3 -X utf8 scripts/boot_check.py --cycles 5 --idle-seconds 1800 --ble --log out/boot-check.json
```

主机测试不打开串口。实机检查需要 pyserial；`--ble` 另需 bleak 和电脑蓝牙，
默认串口 COM5、1 Mbps，可用 `--port` 调整。检查失败或中断会返回非零退出码。
`--check-tf` 可要求真实 FAT 挂载；空 `/data/tf` 目录不算成功。
本工具不会播放音频、修改骑行记录或格式化存储，但会执行 RTS 复位，清空 `/data` 内存盘。
完整实物验收仍需用户确认滑动、菜单、抬腕、帧率，以及需要时的音频听感和断电冷启动。

`scripts/companion_basic_check.py --log out/companion-basic.json` 单独验证安卓 0.1.1
对应的握手、校时和记录读取，包含两次连接。它会同步 RTC、在支持时清除临时定位，
不注入 GPS 数据或写骑行记录。电脑 BLE 验证不能替代安卓真机验收。

## 传感器与驱动主机测试

硬件接入阶段另提供 `bash tests/hardware/run.sh`：从当前实际驱动提取 I²C/ADC 函数，
配合 HAL 适配器覆盖寄存器请求、错误与 IRQ 等待、ADC 槽/均值/分压/失败；并直接编译
后台设备服务验证短读、错误通道、无效身份和缓存过期。包含 ASan/UBSan。
这仍是主机测试；实际身份、电压和 BLE 读回见 `docs/HARDWARE_STAGES.md`。

S3 增加实际 LTR303 驱动源码的身份/激活/待机、四字节一致读取、照度公式各分段、
饱和/总线错误/失败清理；设备服务测试包括晚于开机启动的首样本等待、光线失效、
自动调光限幅/步进/迟滞/回退，以及设置保存、损坏校验与失败重试。
2026-10-10 增加 MMC5603 解码/超时/生命周期、地磁校准和持久化测试；亮度测试更新为
250 ms 新样本节拍，验证两秒增亮、连续插值、死区及过期回退。指南针测试使用合成球面样本，不能证明实物北向准确。
长时稳定性修复新增 `arch_timer` 实际函数测试，显式模拟 ARM 32 位乘法，覆盖 71 分钟微秒回绕、较长 uptime、读数期间中断重试及秒边界采样。

这些测试在 Linux/WSL 主机执行，使用真实应用源码。传感器、Flash 和连接由测试适配器提供，不能替代手表显示、触摸、供电与 BLE 的实物验收。

## 骑行状态

```bash
bash tests/ride_session/run.sh
```

直接编译 `app/velaride/velaride_ride_session.c`，启用 AddressSanitizer、UndefinedBehaviorSanitizer 和编译告警检查。
10 组场景覆盖目标与暂停、分段静止不误报、近期冲击与提醒优先级、过期冲击、传感器失联恢复、记录 CRC/保存失败、复盘分块、连接状态、手机定位数据生命周期与并发快照一致性。

## 界面交互与渲染

以下路径对应当前本机 WSL；在其他工作区替换 `LVGL_SOURCE` 为相同项目版本的 LVGL 源码目录。

```bash
cmake -S tests/sport_ui -B /tmp/velaride-product-ui-test -GNinja \
  -DLVGL_SOURCE=/home/wyj/velaride_ai/apps/graphics/lvgl/lvgl \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build /tmp/velaride-product-ui-test -- -j8
mkdir -p /tmp/velaride-ui-screens
/tmp/velaride-product-ui-test/sport_ui_test /tmp/velaride-ui-screens
/tmp/velaride-product-ui-test/shell_ui_test /tmp/velaride-ui-screens
```

测试使用实际 LVGL、固件中文字体和 `velaride_sport_ui.c`，通过 LVGL 事件点击真实按钮并推进测试时钟。
覆盖取消倒计时、暂停/继续、结束确认、目标达成后继续、冲击提醒、无记录/无建议、长建议滚动和保存失败后的内存保留与重试。
S3 另覆盖设备页进入屏幕设置、三档手动亮度、自动开关及逐级返回。
本轮再覆盖图标设置列表、校准/取消、未就绪与方向预览等状态。
导出的 PPM 是软件渲染截图，不是手表摄像照片；测试中的时长、连接与传感器数据均为模拟输入。

`shell_ui_test` 从实际壳层源码提取 NSH 入口之前的 UI 部分，渲染五个原生表盘，并以
LVGL 指针事件验证七个中文菜单入口的路由。排除硬件诊断入口，设备数据和页面接收端使用适配器。
功能键测试使用真实按键状态处理函数，覆盖单击菜单、双击/快速连按、持续按住和页面返回，确认按键不再进入骑行。
主机测试未覆盖真实触摸硬件采样、物理按键时序、NVS 的芯片级行为或屏幕复位恢复；这些需要同一候选固件在实物上验证。
