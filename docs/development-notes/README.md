# VelaRide AI 项目文档

**原则：这里只写真机测出来的数字和读过的源码。**推断和猜测不进文档。

2026-08-28 清理过一轮 —— 旧文档里有若干未经验证的结论被实测推翻，
已删除。旧版存档在 `backup/project-docs-old-20260828/`，仅供追溯，不要当依据。

被推翻的例子：

- 「LVGL 没有 EPIC 后端，要自己写渲染后端」 → 官方 `middleware/lvgl/lv_drivers_v9/sifli/` 已有完整 v9 后端
- 「EPIC 依赖 RT-Thread，openvela 上用不了」 → HAL 层零依赖，已在真机跑通；驱动层依赖也很浅
- 「帧率优化已做，30→60fps」 → 改错了配置项，且真实瓶颈在内存带宽，不在刷新周期

## 文档

| 文件 | 内容 |
|---|---|
| [PERF_FINDINGS.md](PERF_FINDINGS.md) | 帧率与内存带宽实测数据、EPIC 跑通的必要条件、排障工具 |
| [EPIC_UPSTREAM.md](EPIC_UPSTREAM.md) | 官方 SiFli-SDK 里的 EPIC / GPU 方案，含源码路径与限制 |
| [BUILD_FLASH.md](BUILD_FLASH.md) | 编译烧录流程、踩过的坑（这份是天天在用的，已验证） |
| [SPORT_UI_20260829.md](SPORT_UI_20260829.md) | 五表盘、蜂窝菜单、按键规则、运动流程与真机验收状态 |
| [RTC_BOOT_TIME_20260830.md](RTC_BOOT_TIME_20260830.md) | 上电时间恢复、RTC 限制和校时方案 |
| [WEB_GATEWAY_BLE_PROTOCOL.md](WEB_GATEWAY_BLE_PROTOCOL.md) | Web 网关、最小 BLE JSON 协议、AI 复盘接口和验证状态 |

## 当前状态

| 项 | 状态 |
|---|---|
| 编译烧录 | 一条命令，增量约 25s / 全量约 90s |
| 屏幕 / 触摸 / RTC / 六轴 / ADC | 真机实测通过 |
| 快应用四卡片上屏 | 通过（swiper 竖滑） |
| 滑动显示吞吐 | LCD `putarea()` 调用率显著提升；局部刷新下不能直接等同显示 FPS |
| 触摸跟手性 | 独立 16ms 帧泵已上屏实测；输入 70–90Hz，但有效刷新仅 6.5–9.5Hz、端到端约 110–155ms，主瓶颈已收敛到 CPU 整屏软件重绘 |
| EPIC 硬件加速 | HAL 层已跑通，但纯填充/拷贝比 CPU 慢；LVGL 后端移植暂缓，待真实刷新/触摸测量后决定 |
| velaclaw AI 链路 | 请求可达 ai_agent 守护进程 |
| AI 真正回话 | 缺网络 + LLM Key |

已解决全屏 framebuffer 位于 PSRAM 导致的主要渲染吞吐问题：历史 60 行局部刷新
实验中，LCD `putarea()` 调用率从 2.9 提升到 36.5；当前固件使用 120 行局部缓冲。
进一步复核发现，完整 NuttX libuv FB 集成会删除 LVGL 默认刷新 timer，而 LCD fd
始终报告可写。第一版仅保留 `TIMER | INPUT` 时默认 timer 没有可靠地产生首帧，真机
黑屏；第三版在 UI 建成后同步提交首帧，再用独立 16ms libuv 定时器驱动刷新，画面
恢复。30 秒真机采样证明触摸和刷新唤醒足够快，主要延迟位于同步 CPU 整屏重绘；
下一阶段应移植思澈完整 EPIC draw unit、双 50 行 SRAM 缓冲和异步 LCD flush。详见
[PERF_FINDINGS.md](PERF_FINDINGS.md)。

当前二维手表导航已包含五个表盘、自由二维蜂窝菜单和单目标 PSRAM 转场快照；
KEY2 单击切换蜂窝、双击进入运动流程。运动 UI 已完成候选固件和冷启动验证，
完整触控流程及 25–30 Hz 目标仍待真机人工验收。详见
[SPORT_UI_20260829.md](SPORT_UI_20260829.md) 和
[NAVIGATION_ARCHITECTURE.md](NAVIGATION_ARCHITECTURE.md)。

## 注意

`docs/` 是 openvela 官方文档仓库（repo sync 拉的，576 个 md，有自己的 git）。
查 openvela API 才去翻它，了解本项目现状不要读。

我们自己的文档一律放这里（`project-docs/`）。
