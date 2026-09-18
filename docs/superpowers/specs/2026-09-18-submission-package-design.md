# VelaRide AI 参赛提交包设计

## 目标

把现有分散在 openvela 多仓工作区中的 VelaRide AI 成果，整理为专属比赛仓
`contest2026_446_nandongtaxin` 可直接评审、可复现、可追溯的提交包。

## 提交口径

- 作品方向：AI 硬件产品创新，兼具手表应用创新。
- 稳定演示链路：手表独立完成骑行与本地记录，Web 网关录入真实结果并调用
  Xiaomi MiMo 生成复盘。
- BLE 协议和设备端实现保留，但 SF32LB52 LCPU HCI TX 时序问题尚未解决，
  不宣称无线闭环已经稳定完成。
- Web/COM5 运行时桥接不进入演示流程，避免 CH340 复位线耦合导致黑屏。
- 未完成的快应用草稿不纳入主提交。
- AI Coding 日志不伪造、不手工改写；本次整理不安装采集器，只保留官方说明和
  `logs/WYJ900/` 预留目录。

## 仓库结构

```text
app/velaride/                 原生 LVGL/NuttX 应用与资源
agent_skills/ride-safety.md   自定义 AI Agent Skill
web-gateway/                  本地 Web 网关与 MiMo 服务
patches/                      公共仓受控修改
overlays/                     公共仓新增文件
scripts/                      应用补丁、构建和校验脚本
docs/                         架构、验证、构建、视频说明
artifacts/                    已验证固件和 SHA-256
submission/                   作品介绍 PPTX
logs/                         官方 AI Coding 日志目录
```

## 公共仓集成策略

团队自有源码完整放入专属仓，通过 manifest `linkfile` 映射：

- `app/velaride` → `apps/examples/velaride`
- `agent_skills/ride-safety.md` → `packages/ai_agent/agent_skills/ride-safety.md`

已有公共仓文件的修改以 Git patch 保存：

- `apps.patch`：LVGL EPIC 编译集成、NuttX libuv 线程池替换。
- `nuttx.patch`：NVS 初始化安全修复、LCD 异步送屏 ioctl。
- `packages-ai-agent.patch`：内置 Skill 注册与最小命令白名单。
- `vendor-sifli.patch`：黄山派触摸、LCD、Flash、RTC、EPIC 与板级配置。

公共仓中的新增文件放入 `overlays/`，由脚本复制到正确位置。脚本必须支持
“首次应用”和“已经应用”两种状态，不能静默覆盖冲突。

## 文档与发布物

- 根 README 作为评委入口，说明作品、目录、构建、运行、演示与限制。
- 文档只保留经过真机或自动测试支持的结论。
- 固件采用已有明确真机验证记录的
  `nuttx_web_mimo_dynamic_buffers_bright_verified_20260830_215000.bin`。
- 作品介绍 PPTX 采用 16:9 中文排版，内容与 README、视频口径一致。

## 验证范围

本轮不自动执行完整 openvela 固件 build。执行以下轻量检查：

- Web 网关 Node 测试。
- manifest XML 解析和 linkfile 路径检查。
- patch `git apply --check`。
- 提交仓敏感信息扫描。
- 文件大小、固件 SHA-256、Git 状态检查。
- PPTX 结构、布局、字体和逐页渲染检查。

## 风险

- 公共仓修改仍需后续按官方流程向对应仓提交 PR。
- 最终固件与整理后的干净仓尚未重新全量构建，因此 README 必须披露固件来源和
  本轮未重建事实。
- 表盘视觉参考 SiFli-SDK，提交包必须保留 Apache-2.0 来源说明；不使用 Apple
  Watch 参考截图作为提交材料。
- `logs/WYJ900/` 在正式提交前仍需放入官方工具导出的真实日志。
