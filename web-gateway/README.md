# VelaRide Web 网关

最小参赛演示网关，替代第一阶段的完整 iPhone App。功能包括：

- Chrome/Edge 预留 Web Bluetooth 连接 `VelaRide` 的协议实现；
- Web Serial/COM5 仅保留为开发诊断代码，稳定演示中禁用；
- 连接后自动发送当前 Unix 时间；
- 模拟手机 GPS 速度和距离；
- 读取手表骑行状态；
- 调用 Xiaomi MiMo 或本地降级逻辑生成骑行复盘；
- 将复盘回传手表并显示在运动结束页。

## 启动

录制演示视频时，推荐使用安全启动器。它会以隐藏输入方式读取 Token，仅放入当前
Node 进程的环境变量，不写入仓库或 `.env`：

一键演示（推荐）：

```powershell
cd <你的仓库>\web-gateway
.\start-demo.ps1
```

该启动器隐藏读取 Token、启动网关、用独立应用窗口打开页面，并在退出时清理环境
变量与进程。页面里点“录入真实手表结果”即按手表结束页预填读数，再点“生成复盘”。

手动方式：

```powershell
cd <你的仓库>\web-gateway
.\start-mimo.ps1 -Model mimo-v2.5-pro
```

然后使用本机 Chrome 或 Edge 打开：

```text
http://127.0.0.1:4173
```

当前稳定演示不连接 BLE 或 COM5：先在手表完成骑行，再点击“录入真实手表结果”，
照手表结束页填写数据并生成 MiMo 复盘。

## Xiaomi MiMo

服务端使用 OpenAI-compatible `chat/completions` 接口。环境变量：

```powershell
$env:MIMO_API_KEY = "你的 Token"
$env:MIMO_BASE_URL = "https://api.xiaomimimo.com/v1"
$env:MIMO_MODEL = "mimo-v2-flash"
npm start
```

如果没有 Key、请求超时或接口失败，服务端自动生成确定性中文复盘，并在网页标记
为“端侧规则降级”。API Key 仅保存在服务端环境变量，不会下发浏览器。

Token Plan 的 Anthropic-compatible 配置也受支持：

```powershell
$env:ANTHROPIC_AUTH_TOKEN = "你的 Token"
$env:ANTHROPIC_BASE_URL = "https://token-plan-cn.xiaomimimo.com/anthropic"
$env:ANTHROPIC_MODEL = "mimo-v2.5-pro"
npm start
```

仅在必须手动排障时使用上述环境变量方式。日常录制优先执行 `start-mimo.ps1`。
不要把 Token 写入仓库、`.env`、截图、演示日志或聊天消息；一旦暴露应立即撤销并
重新生成。

## 浏览器限制

- 支持：桌面 Chrome、桌面 Edge、部分 Android Chrome；
- 不支持：iPhone/iPad Safari（WebKit 没有 Web Bluetooth API）；
- 如果最终必须在 iPhone 上直连，需要增加很薄的 CoreBluetooth 原生壳，网页和
  服务端接口可以继续复用。

## USB 串口模式

当前稳定演示禁止打开 COM5：CH340 与复位线耦合，运行时访问可能引起复位、黑屏
或卡死。以下命令与桥接代码仅保留用于离线开发诊断，不在录制视频时执行：

- `velaride time-set <epoch>`
- `velaride phone-data <speed> <distance>`
- `velaride ride-status`
- `velaride review-hex <utf8-hex>`

不要在手表亮屏演示期间运行串口工具或调用 `/api/device`。

## BLE 协议

- Service：`FFF0`
- TX Notify：`FFF1`
- RX Write：`FFF2`
- 帧格式：UTF-8 JSON，以 `\n` 结束；ATT 写和通知均允许分片。

命令：`ping`、`time_sync`、`phone_data`、`ride_status`、`ride_review`。
