# 验证结果

## 真机验证

| 项目 | 结果 | 证据范围 |
|---|---|---|
| LCD、触摸、RTC、LSM6DSL、ADC | 通过 | 冷启动、交互与设备节点实测 |
| 五表盘和蜂窝菜单 | 通过 | 真机上屏与手势操作 |
| 中文运动流程 | 通过 | 类型、目标、开始、暂停、继续、结束、复盘 |
| 骑行记录持久化 | 通过 | 写入、RTS 复位、重新烧录后三次一致 |
| AI 复盘分块持久化 | 通过 | KVDB 分块、CRC、跨复位读取 |
| Web 网关健康与复盘 API | 通过 | HTTP 冒烟与 Node 自动测试 |
| Xiaomi MiMo 调用 | 通过 | Anthropic-compatible 实际请求 |
| BLE 无线闭环 | 未通过 | LCPU HCI TX 提交时序阻塞 |

## 端到端抓取证据（2026-09-18）

`scripts/capture-demo.mjs` 用本机 Chrome 无头模式驱动真实页面，走的是和人工演示
完全相同的路径：点击“录入真实手表结果”→ 按手表结束页预填读数 → 点击“生成复盘”
→ 断言页面出现“由 Xiaomi MiMo 生成”。任何一步拿到降级结果，脚本以非零码退出，
不出图。

本次结果：

| 项目 | 值 |
|---|---|
| 服务端 AI 模式 | `mimo-anthropic` |
| 预填读数 | `elapsed_s=8, moving_s=5, still_s=1, pause_count=1, impacts=0` |
| 页面来源标记 | 由 Xiaomi MiMo 生成 |
| MiMo 原文 | `骑行8秒，移动5秒，暂停1次。注意保持节奏连贯，避免频繁停顿。` |
| 截图 | `docs/web-mimo-review.png` |

预填读数即演示视频中手表“本次骑行记录”页的原始数字，未做任何放大或编造。

## Web 自动测试

当前 `web-gateway/test/` 包含 4 项测试：

1. 本地降级复盘生成；
2. 健康接口与复盘接口；
3. Anthropic-compatible MiMo Bearer Token 和模型参数；
4. 安全启动脚本不硬编码 Token。

运行：

```bash
cd web-gateway
npm test
```

## 固件

- 文件：`artifacts/velaride-huangshan-pi-verified.bin`
- 大小：9,655,716 字节
- SHA-256：`E2304000A9F7260DA7EBCC53CC2FDE27BBF51237CF758FA71F76B66E369031F1`
- 验证时间：2026-08-30
- 验证内容：动态缓冲修复后真机亮屏，时间、骑行记录、Web/MiMo 与持久化功能保留。

## 性能说明

历史 60 行局部刷新实验中，LCD `putarea()` 调用率由 2.9 提升至 36.5。后续真机
测试显示触摸输入可达 70–90 Hz，但复杂页面有效刷新约 6.5–9.5 Hz，端到端延迟约
110–155 ms。五表盘常驻预览缓存使正常持续滑动基本达到 25–30 Hz；快速短划和
反向落页仍可能波动。

原始记录见 `development-notes/PERF_FINDINGS.md`、
`development-notes/RIDE_PERSISTENCE_20260829.md` 和
`development-notes/WEB_GATEWAY_BLE_PROTOCOL.md`。
