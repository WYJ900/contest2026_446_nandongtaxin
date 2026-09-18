# VelaRide AI Submission Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有 VelaRide AI 成果整理为专属比赛仓可提交、可复现的完整交付包。

**Architecture:** 团队自有应用、Web 和 Skill 直接入仓；公共仓已有文件改动保存为 patch，新增文件保存为 overlay；manifest 和集成脚本负责还原完整工作区。README、验证文档、固件和作品介绍 PPTX 共用同一完成度口径。

**Tech Stack:** openvela/NuttX、C、LVGL、JavaScript/Node.js、PowerShell/Bash、Git patch、PPTX

**Spec:** `docs/superpowers/specs/2026-09-18-submission-package-design.md`

## Global Constraints

- 只修改 `my-contest-repo/`，不删除原工作区成果。
- 不纳入未完成快应用草稿和无关行尾改动。
- 不写入任何 MiMo Token、私钥或本机凭据。
- 不伪造或手工改写 AI Coding 日志。
- 不宣称 BLE 无线闭环已经稳定完成。
- 不自动运行完整固件 build。

---

### Task 1: 仓库骨架与核心源码

**Files:**
- Create: `app/velaride/**`
- Create: `agent_skills/ride-safety.md`
- Create: `web-gateway/**`
- Modify: `contest2026_446_nandongtaxin.xml`
- Create: `.gitignore`

- [x] 复制原生应用、Skill 和 Web 网关，排除缓存与凭据。
- [x] 更新 manifest 的两个 linkfile。
- [x] 删除 hello 示例和日志示例。
- [x] 检查所有 linkfile 源文件存在。

### Task 2: 公共仓补丁与覆盖文件

**Files:**
- Create: `patches/apps.patch`
- Create: `patches/nuttx.patch`
- Create: `patches/packages-ai-agent.patch`
- Create: `patches/vendor-sifli.patch`
- Create: `overlays/apps/system/libuv/nuttx_threadpool.c`
- Create: `overlays/vendor/sifli/chips/drivers/epic/**`
- Create: `scripts/apply-integration.ps1`
- Create: `scripts/apply-integration.sh`

- [x] 从各公共仓导出仅与 VelaRide 有关的 tracked diff。
- [x] 复制必要的新文件，不复制快应用产物和实验工具。
- [x] 编写可重复运行的补丁应用脚本。
- [x] 使用 `git apply --check` 验证补丁可应用。

### Task 3: 文档、固件与提交说明

**Files:**
- Modify: `README.md`
- Create: `LICENSE`
- Create: `THIRD_PARTY_NOTICES.md`
- Create: `docs/README.md`
- Create: `docs/BUILD_FLASH.md`
- Create: `docs/VALIDATION.md`
- Create: `docs/KNOWN_LIMITATIONS.md`
- Create: `docs/VIDEO_SCRIPT.md`
- Create: `artifacts/velaride-huangshan-pi-verified.bin`
- Create: `artifacts/SHA256SUMS`
- Create: `artifacts/README.md`

- [x] 重写评委入口 README。
- [x] 迁移并收敛现有验证文档，统一测试数量和缓冲配置口径。
- [x] 复制有明确真机验证记录的固件并生成 SHA-256。
- [x] 补充第三方来源、未完成能力和提交前人工步骤。

### Task 4: 作品介绍 PPTX

**Files:**
- Create: `submission/VelaRide_AI_作品介绍.pptx`

- [x] 使用项目真实素材制作中文 16:9 PPTX。
- [x] 内容覆盖痛点、方案、真机功能、技术架构、验证结果、AI Skill 和限制。
- [x] 运行 PPTX finalizer，渲染并逐页检查。

### Task 5: 提交前轻量验收

**Files:**
- Create: `scripts/check-submission.ps1`

- [x] 运行 Web 网关测试。
- [x] 检查 XML、链接源、补丁、敏感信息和固件哈希。
- [x] 运行 `git diff --check` 和 `git status --short`。
- [x] 输出仍需用户完成的 GitHub fork/PR、CLA、真实日志和上传入口事项。
