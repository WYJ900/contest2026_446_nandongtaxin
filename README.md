# VelaRide AI 骑行安全腕上助手

VelaRide AI 是运行在 openvela 黄山派上的骑行安全原型。手表端负责腕上交互、
运动检测、本地安全规则和骑行记录；Web 网关通过 BLE 读取真实骑行数据，调用
MiMo 或兼容 OpenAI / Anthropic 的服务与中转站，生成中文复盘并回传。

**决赛版（2026-11-06）先读：[工程总览](docs/PROJECT_GUIDE_20261106.md)**——系统拓扑、当前固件/App 版本与哈希、功能验证状态、代码地图、演示启动顺序和视频脚本都在这一页。

当前真机：手表 `424e7a2c`（`artifacts/phone-bridge-20261106/`）、真我手机 App 0.3.6、openvela 模拟器 ai_agent + 自定义 Skill `agent_skills/ride-coach.md` 经 MCP 操作真手表。

以下为早期（2026-10）说明，保留作历史记录。

**继续开发先读：[当前成果与接续说明](docs/CURRENT_STATE.md)（2026-10-10）。**
最新接续：[2026-10-10 系统计时修复](docs/TIMER_STABILITY_20261010.md)，解决约 71 分钟后的微秒换算回绕。[手表设置与硬件功能](docs/WATCH_PRODUCT_20261010.md)继续保留，指南针实物方向验证尚待完成。
其中固定了最后安装的固件校验值、已验证范围、黑屏恢复边界、源码位置和下一步顺序。
当前已完成真实链路验证，仍有显示配置丢失触发源、离线走时和长期稳定性待解决。

当前在[产品体验候选](docs/PRODUCT_ITERATION_20261009.md)上继续完成了
[硬件阶段 S0/S1](docs/HARDWARE_STAGES.md)：USB 检测、后台参考电压、设备状态页及 I²C/ADC 基础修正。
正在验收 [S3 环境光与屏幕设置](docs/AMBIENT_DISPLAY_20261009.md)：真实光线采样、三档亮度、可关闭的自动调光及设置保存。
产品界面包含中文功能菜单、真实时长目标、出发检查、可取消倒计时、可滚动复盘及真实数据表盘。
新记录闭环验收仍在进行，显示恢复基线镜像继续保留。

## 参赛方向

- 主要方向：AI 硬件产品创新
- 组合方向：手表应用创新
- 开发板：立创黄山派，SF32LB52
- 团队仓：`contest2026_446_nandongtaxin`
- GitHub：`WYJ900`

## 当前可演示成果

设备端已经完成：

- 五个表盘、二维滑动导航和蜂窝菜单；
- 中文运动配置、开始、暂停、继续、结束确认和本地复盘；
- LSM6DSL 运动/静止采样、冲击后静止、长时间静止和连续运动提醒；
- 有效 RTC 时间保留、无有效时间时恢复上次保存值，以及网页连接后的自动校时；
- NOR Flash 独立配置区、NVS/KVDB、CRC32 骑行记录与 AI 复盘持久化；
- `ride-safety` Skill，以及只允许四个骑行业务命令的最小权限控制。

Web 网关已经完成：

- `GET /api/health` 和 `POST /api/ride/review`；
- OpenAI Chat Completions、OpenAI Responses、Anthropic Messages 兼容接口；
- 自定义中转站地址、模型名、Bearer / x-api-key 鉴权，保留旧 MiMo 配置；
- 无 Key、超时或接口失败时明确标记本地规则降级；
- BLE 校时、实时状态、最近记录读取、复盘回传确认，以及手工录入；
- 41 项 Node 自动测试（2026-11-06），覆盖协议、MCP 手表工具、Agent 看板数据流、数据校验、字库一致性和失败处理。

已完成真实验证的演示链路：

```text
手表独立完成骑行和本地记录
              ↓
Web 网关通过 BLE 读取最近骑行记录
              ↓
配置的 AI 服务生成复盘 → BLE 回传并持久化
```

2026-10-09 已修复共享内存缓存、堆覆盖 mailbox、GATT 回调发送和断线广播问题，
并完成真实蓝牙与中转站调用。最新验证范围见 [升级记录](docs/HARDWARE_UPGRADE_20261009.md)。
USB 网页网关仍默认关闭。

## 目录结构

```text
app/velaride/                 原生 NuttX/LVGL 应用和编译资源
agent_skills/                 ai_agent Skill：ride-coach.md（决赛版）、HEARTBEAT.md、ride-safety.md
mobile/android/               真我/安卓手机 App（摔倒呼救、消息天气、轨迹路线、AI 教练）
web-gateway/                  本地网页、Node 服务和自动测试
patches/                      apps、nuttx、ai_agent、vendor/sifli 修改
overlays/                     公共仓新增的 libuv 与 EPIC 文件
scripts/                      集成、构建和提交检查脚本
docs/                         架构、构建、验证、视频和限制说明
artifacts/                    真机验证固件与 SHA-256
submission/                   作品介绍 PPTX
logs/                         官方 AI Coding 日志目录
```

## 获取完整工作区

```bash
repo init -u https://github.com/open-vela/contest2026_446_nandongtaxin \
  -b dev-ai-contest-2026 -m contest2026_446_nandongtaxin.xml
repo sync -c -j8
```

manifest 会将本仓的原生应用和 Skill 映射到 openvela 编译树。公共仓改动需要再应用
一次受控补丁：

```bash
cd contest2026_446_nandongtaxin
bash scripts/apply-integration.sh --check
bash scripts/apply-integration.sh
```

Windows PowerShell 可使用：

```powershell
cd contest2026_446_nandongtaxin
.\scripts\apply-integration.ps1 -Check
.\scripts\apply-integration.ps1
```

脚本可重复运行：已应用的 patch 或 overlay 会显示 `ALREADY`；遇到内容冲突会停止，
不会静默覆盖。

## 编译

Ubuntu 22.04 环境下，在完整工作区执行：

```bash
cd contest2026_446_nandongtaxin
bash scripts/build-firmware.sh
```

板级配置为：

```text
vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh
```

固件输出以实际 openvela 构建日志为准。当前硬件阶段见 `artifacts/velaride-hardware-s01-20261009.bin`，
上一轮产品候选为 `artifacts/velaride-product-20261009.bin`，
显示恢复回退基线为 `artifacts/velaride-huangshan-pi-20261009.bin`。
已在现有 WSL 工作树重新编译并烧录；尚未在全新 checkout 完成复现，详见 [构建说明](docs/BUILD_AND_RUN.md)。

## Web 网关

需要 Node.js 20 或更高版本：

```powershell
cd web-gateway
npm test
.\start-ai.ps1
```

`start-ai.ps1` 支持选择接口协议、自定义中转站地址和模型，隐藏读取 Key 后启动网关。
页面显示实际配置的服务和模型；只有成功取得正文才标记为 AI 生成，失败会明确降级。
旧的 MiMo 专用 `start-demo.ps1 -Capture` 仍保留，用于重现历史演示。

安全启动器会隐藏读取 Token，只写入当前 Node 进程环境，不写入文件。浏览器访问：

```text
http://127.0.0.1:4173
```

在手表完成骑行后，网页依次点击“连接手表”“读取最近骑行记录”“生成复盘”
和“回传复盘到手表”。设备确认保存后会自动显示复盘。也可手工录入；“历史样例”
入口单独标记旧记录，不能当作本次骑行数据。

## 验证证据

- LCD、触摸、RTC、LSM6DSL 和 ADC 已真机验证；
- 骑行记录在写入后、RTS 复位后、重新烧录同一固件后三次读取一致；
- Web 页面、健康接口、复盘接口和 MiMo 兼容协议已验证；
- 固件中的 Web/MiMo 动态缓冲修复完成真机亮屏验证；
- Web 网关自动测试当前为 29/29（2026-10-09）。

验证边界和原始记录见 [验证说明](docs/VALIDATION.md) 与
[`docs/development-notes/`](docs/development-notes/)。
最终显示修复版与早期链路验证的区别见 [显示恢复记录](docs/DISPLAY_RECOVERY_20261009.md)。

## AI Coding 与 Skill

AI 用于需求拆解、openvela 源码定位、驱动排障、界面实现、性能测量、Web 服务、
测试和文档整理。自定义 `ride-safety` Skill 规定：

- 只读取骑行状态和最近记录；
- 只有用户明确确认后才能执行暂停或继续；
- 不允许调用持久化格式化等维护命令；
- 网络失败不能覆盖手表本地计时、提醒和记录；
- 复盘不能虚构速度、距离、心率或定位数据。

官方工具导出的真实 AI Coding 日志应提交到 `logs/WYJ900/`。本仓不接受手工构造
或修改过的日志。

## 已知限制

- 尚未覆盖长期户外运行、不同蓝牙适配器与低功耗恢复；
- COM5 与 CH340 复位线耦合，稳定演示中禁用运行时串口网关；
- GPS、心率传感器和 iPhone 原生网关尚未接入；
- 字库外字符先尝试同一模型的兼容改写，失败才明确回退规则短版；
- 公共仓修改最终仍需按大赛流程分别向对应 `dev-ai-contest-2026` 分支发 PR。

完整说明见 [已知限制](docs/KNOWN_LIMITATIONS.md)。

## 开源许可

本作品代码采用 Apache License 2.0。SiFli、NuttX、LVGL 和字体来源说明见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
