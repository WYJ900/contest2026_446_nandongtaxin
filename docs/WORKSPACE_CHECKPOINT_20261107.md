# 工作区源码归档与蓝牙消息演示（2026-11-07）

本次按用户要求保存工程并推送个人仓库。没有修改或刷写手表固件，没有把旧工作树覆盖到决赛版应用。

## 已验收版本与归档边界

当前版本仍以 [工程总览](PROJECT_GUIDE_20261106.md) 为入口：手表固件 SHA-256 `424e7a2c41fe57dbd581e1c313bfe8793067415e0af449166700f501d3c1a734`，源码快照在 `artifacts/phone-bridge-20261106/app-source.tar.gz`，安卓 App 为 0.3.6。

[工作区检查点](../artifacts/workspace-checkpoint-20261107/README.md) 另存了 530 份源码/支持文件、15 份多仓库差异补丁、基准提交号和文件哈希。包含原工作区 `tools/`、`project-docs/`、快应用源码、部分构建实验源码、外壳建模脚本，以及 apps/NuttX/LVGL/蓝牙/SiFli 等仓库的改动。压缩包保留原工作区相对路径。

这是一份源码保全归档，不是新验收的固件版本，也不是完整可重建工作区。原工作区的 `apps/examples/velaride` 与决赛版源码不同；根目录 `web-gateway/server.mjs` 比本仓旧，缺少 MCP 和 Agent 看板接口，所以本仓正式目录继续保留决赛版。历史源码需解压到单独目录比较，不能直接覆盖当前可用版本。

未收录私钥、环境凭据、原始录音录像、依赖缓存、完整 SDK/工具链、WSL 构建树或生成的 CAD 网格；具体范围和省略路径见检查点 `manifest.json`。现有已验收固件、配置与源码快照保持原样。

发现 `artifacts/_tmp_gps_fresh/firmware.bin` 尚未入仓，已按原始字节保存在检查点的 `unidentified-gps-candidate-21eca243.bin`。其 SHA-256 为 `21eca2436cec6d3e3f80394f57a96ccc167c2fcb19bbe80a287d60c286f9395d`，9,766,724 字节。未在本轮确定完整源码/配置来源，也未烧录或验收，不能作为当前版本或回退基线。

## 电脑模拟手机发送消息

新增 [phone_message_demo.py](../scripts/phone_message_demo.py)，通过电脑 BLE 使用现有的 `phone_weather` 和 `phone_note` 命令。天气和消息为模拟内容，传输及接收端为真实手表。

在有 Python 和 bleak 的 Windows 电脑上运行：

```powershell
py -3 scripts/phone_message_demo.py --count 3 --delay 10 --interval 15
```

连接并同步时间后发送“演示 23.0°C 晴”；等待 10 秒给用户准备录像，再每隔 15 秒发送一条消息，共 3 条。最后等待 8 秒后断开。不会主动清空已有消息，不会启动音频实验，不会更改骑行记录。手表消息仅在 RAM 中保存，天气有效期遵循原有逻辑。

手表点“手机新消息”横幅，或进入“设置 → 手机消息”，查看顶部天气与下方消息卡片。实际屏幕显示、横幅点击和录制效果需要人工观察。

本轮已实际收到手表对 `ping`、`time_sync`、`phone_weather`、`phone_note` 的成功 BLE 应答，见 [原始回执](validation-phone-demo-20261107/test-result.json)。测试消息是“微信 / 骑行群 / 我到公园门口了，路上注意安全。” 用户尚未反馈画面，因此没有把 BLE 应答写成 UI 验收通过。新增定时脚本仅完成命令行加载检查，尚未完整执行三条定时发送流程。

## 已讨论但尚未实现

手机 App 作为 BLE 网络代理、让手表本机 Agent 调用云端模型，目前只是方案讨论。现有 `http_proxy` 基于 TCP，尚未接通 BLE 请求/响应转发。不得将其描述为已实现功能。

表盘来源：本项目实现面向 NuttX/LVGL 的表盘、导航和骑行数据联动，视觉及部分交互参考 SiFli SDK `watch_v9`，字体使用 Noto Sans SC 和 Outfit；来源说明见 [第三方声明](../THIRD_PARTY_NOTICES.md)。
