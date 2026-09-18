# VelaRide AI 骑行安全腕上助手

VelaRide AI 是运行在 openvela 黄山派上的骑行安全原型。手表端负责腕上交互、
运动检测、本地安全规则和骑行记录；Web 网关使用真实骑行数据调用 Xiaomi MiMo，
生成简短的中文复盘。

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
- RTC 时间恢复；
- NOR Flash 独立配置区、NVS/KVDB、CRC32 骑行记录与 AI 复盘持久化；
- `ride-safety` Skill，以及只允许四个骑行业务命令的最小权限控制。

Web 网关已经完成：

- `GET /api/health` 和 `POST /api/ride/review`；
- Xiaomi MiMo OpenAI-compatible 与 Anthropic-compatible 调用；
- 无 Key、超时或接口失败时明确标记本地规则降级；
- 真实手表结果手工录入和 AI 复盘展示；
- 4 项 Node 自动测试。

当前稳定演示链路：

```text
手表独立完成骑行和本地记录
              ↓
Web 网关录入手表结束页的真实数据
              ↓
Xiaomi MiMo 生成骑行复盘
```

BLE 协议与设备端实现已经保留，但 SF32LB52 LCPU HCI TX 时序问题尚未解决，
本次稳定演示不宣称无线闭环已经完成，也不在手表亮屏期间访问 COM5。

## 目录结构

```text
app/velaride/                 原生 NuttX/LVGL 应用和编译资源
agent_skills/ride-safety.md   VelaRide AI Agent Skill
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

固件输出以实际 openvela 构建日志为准。现有真机验证固件见 `artifacts/`；提交整理后
尚未重新执行全量 build，详见 [构建说明](docs/BUILD_AND_RUN.md)。

## Web 网关

需要 Node.js 20 或更高版本：

```powershell
cd web-gateway
npm test
.\start-demo.ps1 -Model mimo-v2.5-pro
```

`start-demo.ps1` 会隐藏读取 Token、启动网关、并用独立应用窗口打开页面。页面里
只需要两步：点“录入真实手表结果”（数值已按手表结束页填好），再点“生成复盘”。
看到“由 Xiaomi MiMo 生成”即为真实调用；显示“降级”说明 Token 或网络未生效。
加 `-Capture` 可同时用无头浏览器抓取一张结果截图，作为真机证据。

安全启动器会隐藏读取 Token，只写入当前 Node 进程环境，不写入文件。浏览器访问：

```text
http://127.0.0.1:4173
```

演示时先在手表完成骑行，再点击“录入真实手表结果”，照手表复盘页填写数据并生成
MiMo 复盘。页面必须显示“由 Xiaomi MiMo 生成”，否则属于降级结果。

## 验证证据

- LCD、触摸、RTC、LSM6DSL 和 ADC 已真机验证；
- 骑行记录在写入后、RTS 复位后、重新烧录同一固件后三次读取一致；
- Web 页面、健康接口、复盘接口和 MiMo 兼容协议已验证；
- 固件中的 Web/MiMo 动态缓冲修复完成真机亮屏验证；
- Web 网关自动测试当前为 4/4。

验证边界和原始记录见 [验证说明](docs/VALIDATION.md) 与
[`docs/development-notes/`](docs/development-notes/)。

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

- BLE 无线闭环仍受板级 HCI TX 时序问题影响；
- COM5 与 CH340 复位线耦合，稳定演示中禁用运行时串口网关；
- 当前 Web 演示需要手工录入手表结束页数据；
- 固件使用的手表中文字为按需子集，网页保留 MiMo 原文，手表短版会过滤字库外字符；
- 公共仓修改最终仍需按大赛流程分别向对应 `dev-ai-contest-2026` 分支发 PR。

完整说明见 [已知限制](docs/KNOWN_LIMITATIONS.md)。

## 开源许可

本作品代码采用 Apache License 2.0。SiFli、NuttX、LVGL 和字体来源说明见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
