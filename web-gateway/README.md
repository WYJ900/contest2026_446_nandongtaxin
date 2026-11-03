# VelaRide Web 网关

当前流程：BLE 连接手表 → 读取最近骑行记录 → AI 复盘 → 等待手表确认回传保存。
也支持手工录入；AI 失败时明确标记网关本地规则降级。USB 运行时连接默认关闭。

## 多种 AI 服务与中转站

执行 `./start-ai.ps1`，在本机窗口选择接口，填写 Base URL、模型名及隐藏输入的 Key。
配置只存在于启动器和 Node 子进程的环境变量中，不写入文件，不发到浏览器。
这支持兼容接口的中转站；特定站点是否可用仍取决于其协议、模型权限和余额。

| 接口类型 | AI_PROTOCOL | 请求路径 | 鉴权 |
|---|---|---|---|
| OpenAI Chat Completions 兼容 | `openai` | `/chat/completions` | Bearer |
| OpenAI Responses 兼容 | `responses` | `/responses` | Bearer |
| Anthropic Messages 兼容 | `anthropic` | `/v1/messages` | Bearer 或 `x-api-key` |

高级配置使用 `AI_BASE_URL`、`AI_MODEL`、`AI_API_KEY`、`AI_PROTOCOL`，可选
`AI_PROVIDER_NAME`、`AI_AUTH`、`AI_TIMEOUT_MS`（1000–60000，默认 60000）。
DeepSeek 模型的短复盘默认关闭思考以减少正文被推理占满的情况；可用
`AI_THINKING=provider` 保留服务商默认行为。该参数按
[DeepSeek 官方兼容说明](https://api-docs.deepseek.com/guides/thinking_mode/)发送。
不配置通用 `AI_*` 时继续兼容旧 `MIMO_*` / `ANTHROPIC_*` 配置。显式指定中转站后
不会借用旧 MiMo Key；地址不得包含 Key、账号密码或查询参数。

地址可填根域名、带 `/v1` 的基础地址或完整接口地址。例如
`https://relay.example/v1` 和 `https://relay.example/v1/chat/completions` 都可用于
`openai`。自定义前缀会保留，完整路径与协议冲突则报告配置错误。

网页显示实际配置的服务与模型，以及未配置、超时、HTTP 失败等降级原因。
手表使用最多 260 字节的 UTF-8 文本。出现字库外字符时，在总超时范围内由同一
模型做一次兼容改写，网页与手表输出相同的最终 AI 正文，并保留 `original_review`
供追溯。改写仍不兼容时明确提示手表使用规则短版，不将它冒充模型原文。

接口依据：[Chat Completions](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)、
[Responses 文本生成](https://developers.openai.com/api/docs/guides/text)、
[Anthropic Messages](https://platform.claude.com/docs/en/api/messages/create)。

## 开发版本与同步

在现有双目录工作区中，以 `my-contest-repo/web-gateway/` 为主版本，根目录
`web-gateway/` 为运行副本。在团队仓目录执行：

```powershell
node scripts/sync-web-gateway.mjs --check
node scripts/sync-web-gateway.mjs --apply
```

同步范围为脚本内列出的 18 个运行、文档和测试文件，不删除目标文件。仅团队仓
提供 `start-demo.ps1` 和截图脚本；根目录副本使用 `npm start` 或 `start-ai.ps1`。

`start-ai.ps1` 使用 Node watch 模式，代码和字库更新可自动重启网关并保留本次进程
中的配置，通常无需再次输入 Key。退出启动器后仍需重新输入。

## 2026-10-09 升级

- “录入真实手表结果”保留已有输入并聚焦表单，不自动填入历史记录。
- “加载历史样例”单独载入 2026-09-18 的 8 秒记录，预览明确显示历史样例。
- 前后端共用校验：总时长为正整数，时间与次数为非负整数，距离为非负有限数字；
  运动与静止时间之和不得超过总时长。API 字段必须为 JSON 数字；可选字段省略时为 0。
- 输入变化时取消等待、清除旧复盘、更新预览；过期请求不能覆盖新记录。
- 区分未配置、云端超时、服务失败；8 秒记录按秒展示。
- `/api/device` 默认返回 403，不启动串口进程。仅离线诊断时可设置
  `VELARIDE_ENABLE_DEVICE_BRIDGE=1`；不要在手表亮屏演示期间启用。

运行 `npm test` 验证所有测试。云端协议使用本机模拟服务测试，不消费真实 Token。

## 启动

录制演示视频时，推荐使用安全启动器。它会以隐藏输入方式读取 Token，仅放入当前
Node 进程的环境变量，不写入仓库或 `.env`：

一键演示（推荐）：

```powershell
cd <你的仓库>\web-gateway
.\start-demo.ps1
```

该启动器隐藏读取 Token、启动网关、用独立应用窗口打开页面，并在退出时清理环境
变量与进程。页面里点“录入真实手表结果”后手工填写，再点“生成复盘”。需要重现
归档演示时使用“加载历史样例”；截图脚本也明确使用该历史样例入口。

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
为“网关本地规则降级”。API Key 仅保存在服务端环境变量，不会下发浏览器。

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
