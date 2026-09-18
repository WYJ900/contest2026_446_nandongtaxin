# 显示吞吐与交互性能实测数据

**2026-08-28 实测于立创黄山派（SF32LB52 / Cortex-M33 / 390×450 RGB565）**

本文只记录真机测出的数字和读过的源码。没验证的推断不写在这里。

## 结论

已确认的主要渲染吞吐瓶颈是 **LVGL 的 framebuffer 分配在 PSRAM 上**。PSRAM 走 QSPI 串行总线，
同一个全屏 memcpy 在 SRAM 上 1ms，在 PSRAM 上 43.7ms —— **差 40 倍**。

历史实验改用局部刷新（60 行分块 = 46KB，落进 SRAM）后，LCD `putarea()` 调用率
2.9 → 36.5 calls/s。该修复解决了主要渲染吞吐瓶颈，但不能单独证明触摸跟手性。
当前固件使用 120 行局部缓冲；本轮固定帧泵实验不调整该参数，避免同时改变两个变量。

## 已修复：局部刷新把 buffer 挪进 SRAM

**历史 60 行实验中，LCD `putarea()` 调用率 2.9 → 36.5 calls/s（12.6 倍）。**

```
CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y
CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=60
```

原理：全屏模式下 draw buffer 是 390×450×2 = 351KB，SRAM 放不下（只剩约
113KB），必然落到 PSRAM。改成 60 行分块后 buffer 只要 390×60×2 = 46KB，
`lv_malloc`（= `malloc`，因为 `LV_USE_CLIB_MALLOC=y`）优先从 region 0 分配，
落进 SRAM。渲染和送屏都在快内存上跑。

`BUFFER_COUNT` 变成 0，LVGL 自动切到 `LV_DISPLAY_RENDER_MODE_PARTIAL`
（见 `lv_nuttx_lcd.c:169-174`）。

## LVGL C 卡片流：已修复渲染掉帧，输入延迟仍需复测

局部刷新解决了主要显示吞吐问题，但手感仍"不跟手"。已有 `putarea()` 日志显示
调用间隔存在明显长尾，但该指标不等于显示帧率，更不能直接代表触摸延迟。快应用的
`<swiper>` 由 JS 引擎驱动，手指移动要经 QuickJS 算位移再通知渲染，原统计窗口在
8ms~120ms 之间剧烈跳动。

改用 LVGL C（`apps/examples/velaride/`）后，LCD 送屏调用的长尾明显减少：

| | 快应用（JS 驱动） | LVGL C |
|---|---|---|
| 平均 `putarea()` 调用率 | 62.2 calls/s | **73.3 calls/s** |
| 最低窗口调用率 | 1.5 calls/s | 10.9 calls/s |
| 低于 20 calls/s 的窗口 | 十几次 | **1 次 / 228** |
| 送屏 `flush_avg` | 3ms | 3ms |

平均值提升不大，LCD 送屏调用的低谷显著减少。快应用版每次切卡片都掉到个位数，
LVGL C 版 228 个采样窗口只有 1 次低于 20 fps。这里的统计来自 LCD
`putarea()`，不能证明触摸到显示的端到端延迟已经解决。

2026-08-28 源码复核发现，原 `velaride` 只调用 `lv_nuttx_init()` 和
`lv_timer_handler()`，没有使用 libuv 事件驱动输入。随后曾直接照仓库
`apps/examples/lvgldemo` 调用完整 `lv_nuttx_uv_init()`；这虽然让触摸 fd 可读时立即
调用 `lv_indev_read()`，却也启用了 `LV_NUTTX_UV_INIT_FB`，删除 LVGL 默认刷新 timer。

继续对比思澈 `SiFli-SDK` 的 `watch_v9` 后发现，NuttX 的 `lcddev_poll()` 明确把 LCD
实现为“永远可写”，`uv_poll_start(..., UV_WRITABLE, ...)` 会立即触发刷新。因此完整
libuv 初始化下，页面每次 `invalidate` 都几乎立即绘制，刷新节拍跟着触摸 IRQ 抖动；
`refr_start_cb()` 虽然照抄官方，触发它的底层时钟却不等价。

首次尝试 `lv_nuttx_uv_init_partial(TIMER | INPUT)` 时依赖 LVGL 默认刷新 timer 的
暂停/恢复机制，2026-08-28 真机烧录后屏幕黑屏，30 秒采样只有每秒数据更新产生的
局部 `putarea()`，没有任何 `VELARIDE_PERF`。第二版不再依赖默认刷新 timer：保留
libuv 的 `TIMER | INPUT`，删除显示默认 timer，新增独立 16ms `uv_timer_t`，直接调用
`_lv_display_refr_timer(NULL)`。第二版仅依赖定时器的首次 0ms 回调，真机仍然黑屏；
进程存活，说明初始全屏失效区在 uv loop 启动前后没有可靠提交。第三版在 UI 构建后
显式全屏 `invalidate` 并同步调用一次 `_lv_display_refr_timer(NULL)` 完成首帧，再删除
默认显示 timer、启动 16ms 帧泵。该方案仍需真机验证，在验证前不写成已解决。

同时修正 FT6146 驱动：原实现把所有按下期间的样本都标为 `TOUCH_DOWN`，且
`touch_sample_s` 未清零。现在按官方 FT6146 数据格式解析 DOWN/MOVE/UP，补齐触点
ID、位置有效位和微秒时间戳；并对齐官方 ISR 策略，在 I²C worker 执行期间关闭
触摸 IRQ，完成后再开启，避免重复投递同一个 NuttX `work_s` 覆盖待处理样本。上述
修复需要真机复测触摸样本间隔和端到端延迟，在复测前不再把“LCD 调用率高”等同于
“手感问题已解决”。

`velaride` 现在会在每次有效滑动松手后输出一行 `VELARIDE_PERF`：`input` 是
`LV_EVENT_PRESSING` 事件率，`refresh` 是以 `LV_EVENT_REFR_READY` 为边界的真实刷新
事务率，两个 `max_gap` 分别表示输入和刷新最大间隔。新增
`latency_start(first/latest)` 与 `latency_ready(first/latest)`：`first` 表示一帧合并的
首个输入到刷新开始/完成的延迟，`latest` 表示最新输入到刷新开始/完成的延迟；同时
打印平均值、最大值和有效帧数。统计在松手后才打印，不会在滑动过程中持续刷串口。

### 16ms 独立帧泵真机结果：刷新调度不是主瓶颈

2026-08-28 第三版独立帧泵恢复画面后，连续上下滑动 30 秒，抓到 40 次有效滑动：

| 指标 | 真机结果 | 结论 |
|---|---|---|
| `input` 平均频率 | 多数 70–90Hz | FT6146 总体上报率足够 |
| `input max_gap` | 多数 118–162ms | 输入仍有明显长尾，但不是平均吞吐瓶颈 |
| 最新输入 → `REFR_START` | 平均约 0.3–1.2ms | 输入合并与帧泵唤醒很快 |
| 有效 `refresh` | 多数 6.5–9.5Hz | 一次同步刷新阻塞了后续 16ms tick |
| 最新输入 → `REFR_READY` | 平均约 102–146ms | 直接对应用户感受到的拖动延迟 |
| 最大端到端延迟 | 多数约 120–159ms，个别超过 300ms | 仍有严重长尾 |
| LCD `putarea()` | 平均 32.9 calls/s，单次约 5–6ms | LCD 单次传输不是 120ms 主耗时 |

局部刷新下，一帧通常包含约 4–5 次 `putarea()`，据此估算 LCD 传输约 20–30ms；
而最新输入到刷新完成通常为 110–155ms，剩余约 90–120ms 主要发生在 CPU 软件布局、
光栅化和整屏滚动重绘。固定 16ms tick 只能按时发起刷新，无法让同步单缓冲渲染并行，
因此体感变化不大符合测量结果。

下一阶段不再优先调整触摸频率、60/120 行或刷新周期。应对齐思澈官方
`watch_v9` 的完整图形链路：LVGL EPIC draw unit、SRAM 双 50 行缓冲以及异步 LCD
flush，并用同一套端到端指标做 A/B 验证。

仓库中已存在适配 LVGL 9.1/NuttX 的同步 EPIC draw unit 与底层驱动，但此前配置关闭。
第一阶段 A/B 启用 `CONFIG_LV_USE_DRAW_EPIC=y`，并将 120 行动态单缓冲改为 60 行：
当前最终链接 SRAM 已使用约 440.7KB，只剩约 83.6KB，120 行 RGB565 需要约 93.6KB，
无法保证落入 SRAM；EPIC 访问不到 PSRAM，会自动回退软件路径。60 行约 46.8KB，作为
EPIC 可访问目标缓冲的必要条件。启动时打印 draw buffer 地址和大小，必须看到
`0x200...` 与 `SRAM/accessible` 才认为 EPIC A/B 前提成立。

首个 EPIC A/B 只让 `LABEL` 任务进入 EPIC，`FILL` 明确保留软件路径：本板已测得
100×100 实色填充 CPU 70µs、EPIC 193µs，当前单步 EPIC 的约 200µs 固定开销会让
小矩形填充变慢。文字后端使用连续 glyph blend，可在一个标签内摊薄配置开销；若该
版本无收益，再基于分阶段耗时决定是否继续 EPIC，而不是盲目把所有任务交给硬件。

同时修正性能统计口径：只有一帧实际应用了非零滚动位移且至少发生一次 LCD flush，
才计入 `valid` 有效帧率；新增 `layout`、`raster`（render 减 flush）、`flush`、
`apply_ready` 的平均/最大耗时，以及 EPIC LABEL 接管数和不可达缓冲回退数。旧的
`putarea calls/s` 只保留作传输调用率，不能再当显示 FPS。

首次可信采样打印 `buffer=0x6005fb38/46800`、`epic_label=0`、
`epic_fallback=825`：即使缩到 60 行，`lv_malloc()` 仍把缓冲分配到 PSRAM，EPIC
完全没有接管，肉眼无变化是必然结果。新增
`CONFIG_LV_NUTTX_LCD_STATIC_SRAM_BUFFER`，将 RGB565 partial buffer 作为 64 字节
对齐静态数组放入内部 SRAM；链接阶段装不下会直接失败，不再静默回退 PSRAM。

静态 SRAM + LABEL-only EPIC 真机采样确认 `buffer=0x20067c80/46800`，EPIC LABEL
实际接管约 2000 次，回退极少；有效刷新仍多为 6–10Hz，`layout` 约 1–3ms、
`raster` 约 60–105ms、`flush` 约 34–43ms、`apply_ready` 约 99–150ms。下一步在
相同静态 SRAM 条件下关闭 EPIC 做 CPU-only 对照，以分离 SRAM 与文字硬件混合收益。

CPU-only 静态 SRAM 现场日志显示 `raster` 约 100–146ms，LABEL-only EPIC 多数约
60–105ms，说明 EPIC 文字混合有收益但不是决定性因素，保留 LABEL-only 策略。
第二阶段新增静态双 32 行 partial buffer（49,920B，只比单 60 行多 3,120B）和异步
LCD DMA：`PUTAREA_ASYNC` 提交后不提前 `flush_ready`，LVGL 在复用 buffer 前通过
`WAITAREA` 等待完成，从而允许一块 DMA 传输时渲染另一块。

首次启动异步版时仍出现同步 `LCD_FLUSH` 日志，定位为 LVGL 适配未填写
`lcddev_area_s.stride`，板级异步入口按安全检查返回 `-ENOTSUP` 并回退同步。现从
active draw buffer 复制真实 stride；只有 full-width/tight 区域进入异步，其他局部
区域继续同步回退。

双 32 行异步版真机验证后，`flush` 降至约 16–27ms，但每帧 15 个 partial 块仍反复
遍历并重画文字，`raster` 约 80–145ms，有效刷新仍约 7–10Hz。完整双卡片 RGB565
快照需要约 702KB，无法放入内部 SRAM，放 PSRAM 后 EPIC 又不可访问。改用滚动 LOD：
按下时隐藏卡片内部文字/进度条，仅平移整屏色块；松手无动画吸附后恢复完整内容。
静止页面不降级，目标是把拖动有效刷新提升到 25–30Hz。

为二维导航释放更大的 DMA/GPU 可达缓冲，性能固件关闭 `CONFIG_ALLSYMS`。该选项仅将
崩溃地址在线翻译成符号名，约占 127KB SRAM；地址型 backtrace 和离线 `System.map`
解析仍保留。释放后把静态异步双缓冲从 32 行提高到 80 行，每帧 partial 块数约从
15 降到 6，避免块级重复遍历抵消异步收益。

同时在 indev 层输出三条低频诊断日志：`VELARIDE_TOUCH: PRESSED`、首次
`PRESSING` 和 `RELEASED pressing=N`。它们用于区分触摸驱动、对象命中/冒泡和
渲染路径，不代表性能指标。

首次烧录后连续滑动却没有任何 `VELARIDE_PERF`，由此确认页面手势回调没有收到
触摸事件。继续对比官方 `mainmenu_cell_add_icons()` 后发现，官方给每个图标设置了
`CLICKABLE | EVENT_BUBBLE | PRESS_LOCK`，而本项目卡片未设置 `EVENT_BUBBLE`，内部
布局对象又会成为更深的点击目标。现已对整屏卡片补齐官方标志，并清除纯布局容器和
进度条的 `CLICKABLE`，保证 `PRESSED/PRESSING/RELEASED` 冒泡到页面回调。

做法照官方 watch_v9 的蜂窝菜单
（`example/multimedia/lvgl/watch_v9/src/gui_apps/main/app_mainmenu.c`）：

```c
lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);      /* 必须显式加 */
lv_obj_set_scroll_snap_y(page, LV_SCROLL_SNAP_CENTER);
lv_obj_set_scroll_dir(page, LV_DIR_VER);
```

踩的坑：`lv_obj_remove_style_all()` 会把 `LV_OBJ_FLAG_SCROLLABLE` 一起清掉，
不显式加回来容器收不到滑动手势 —— 症状是只有每秒定时器刷新的恒定 4.9 fps、
`flush_avg=1ms`（只重绘变化的标签），滑动毫无反应。

## LCD 送屏调用率实测

测量方式：在 LCD 驱动 `putarea` 里累计调用次数与耗时，每满 N 次打串口一行
（`vendor/sifli/boards/sf32lb52/drivers/lcd/sf32lb_lcd.c`，搜
`SF32LB_LCD_FLUSH_WINDOW`）。
抓取脚本 `tools/fps.ps1`。屏幕静止时 LVGL 不刷新、不出日志。局部刷新模式下一次
LVGL refresh 可能包含多个 `putarea()`，所以该值是传输调用率，不是真正的显示帧率；
它适合比较驱动吞吐，不适合单独评价跟手性。

| 场景 | 全屏模式（PSRAM） | 局部刷新 60 行（SRAM） |
|---|---|---|
| 快应用四卡片（文字 + 圆角 + 进度条） | 2.9 calls/s（1.4~4.4） | **36.5 calls/s**（峰值 61.6） |
| 纯色空卡片（零文字零圆角） | 7.1 calls/s（5.6~8.1） | **60~72 calls/s** |
| 送屏耗时 `flush_avg` | 17ms | **3ms** |

改前的两条结论（现已被修复推翻，留作记录）：

- 文字渲染吃掉一半以上性能（2.9 → 7.1）—— 局部刷新后差距缩小，
  说明当时的瓶颈主要是内存带宽而非光栅化本身
- 「纯色空屏也只有 7.1 fps，是软渲染天花板」—— 错。天花板是 PSRAM 带宽，
  换 SRAM 后纯色能到 72 fps

局部刷新后偶发 1.5~6 calls/s 的低谷出现在切换卡片瞬间：新卡片全部内容首次
光栅化，FreeType 中文字形还没进缓存。滑动过程的 `putarea()` 调用率通常为
30~55 calls/s。

## 内存带宽实测

测量程序 `apps/examples/epicbench/`（NSH 下敲 `epicbench`）。

| 操作 | SRAM (0x2000_xxxx) | PSRAM (0x6000_xxxx) |
|---|---|---|
| memset 全屏 351KB | ~1ms | 27.7ms |
| memcpy 全屏 351KB | ~1ms | 43.7ms |

PSRAM 上一次 memcpy 43.7ms，对应理论上限 23fps，加上 LVGL 其余开销落到 7fps。
数字对得上。

## 当前堆布局

`vendor/sifli/chips/sf32lb52/sifli_allocateheap.c`：

```c
up_allocate_heap():  heap_start = g_idle_topstack;      // region 0 = SRAM
                     heap_size  = SRAM_END - g_idle_topstack;
arm_addregion():     kumm_addregion(PSRAM_START, PSRAM_SIZE);   // region 1 = PSRAM
```

| | 容量 |
|---|---|
| SRAM 总量 | 512KB |
| 静态段已用 | 399KB（76%） |
| **SRAM 堆剩余** | **约 113KB** |
| 全屏 framebuffer 需要 | 351KB（装不下） |
| PSRAM 用户堆（`free` 实测 Umem） | 8.4MB |

LVGL 的 draw buffer 走 `lv_malloc` → `malloc`，351KB 在 SRAM 放不下，必然落到 PSRAM。

## EPIC 硬件加速实测

`epicbench` 同时测了 EPIC。**EPIC 能在 openvela/NuttX 上直接调 HAL 跑通**。

必要条件（缺一个就是 `state` 停在 BUSY、`EOF_IRQ` 恒 0）：

1. **buffer 必须在 SRAM** —— EPIC 作为 DMA master 访问不到 PSRAM。
   这是踩了五轮才找到的原因。官方示例用 `L2_NON_RET_BSS_SECT` + `ALIGN(64)`
   静态数组就是为此。
2. **EZIP handle 必须给** —— `HAL_EPIC_Init` 第一行就是
   `epic->hezip->user_data = (void *)epic;`，本板 `HAL_EZIP_MODULE_ENABLED`
   是开的（`bf0_hal_conf_hcpu.h:30`），`hezip` 为 NULL 时空指针写入，
   但 Init 仍返回 HAL_OK。
3. 中断挂 NuttX 向量表：`irq_attach(NX_IRQ(EPIC_IRQn), ...)`，
   `EPIC_IRQn=62` → `nx_irq=78`。

单步模式跑分（100×100 RGB565，buffer 在 SRAM）：

| 操作 | CPU | EPIC | 结果 |
|---|---|---|---|
| fill | 70µs | 193µs | EPIC 慢 2.8 倍 |
| blit | 107µs | 286µs | EPIC 慢 2.7 倍 |

**在 SRAM 上做纯填充/拷贝，EPIC 比 CPU 慢** —— 每次操作约 200µs 固定开销
（配寄存器 + 触发 + 等完成），中小区域完全被这个开销吃掉。

EPIC 的价值不在填充拷贝，在旋转 / 缩放 / alpha 混合这些 CPU 很贵的操作。
详见 [EPIC_UPSTREAM.md](EPIC_UPSTREAM.md)。

## 已改动的配置

改了但**没有解决卡顿**（送屏已非瓶颈），保留是因为无副作用：

| 项 | 原值 | 现值 | 位置 | 效果 |
|---|---|---|---|---|
| `CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER` | 未设 | y | `configs/nsh/defconfig` | 决定是否避开全屏 PSRAM 缓冲 |
| `CONFIG_LV_NUTTX_LCD_BUFFER_SIZE` | — | 120（当前） | 同上 | 已进入局部刷新；60 行曾实测送屏约 3ms，但未解决手感 |
| `SF32LB_LCD_CHUNK_ROWS` | 24（硬编码） | 512 | `drivers/lcd/sf32lb_lcd.c` | 无明显影响 |
| `CONFIG_LV_NUTTX_VSYNC_TIMER_PERIOD` | 33 | 16 | `configs/nsh/defconfig` | 只影响 VSYNC 事件，不直接限制刷新节拍 |

`CUSTOM_BUFFER` 与足够小的行缓冲解决的是 PSRAM 送屏吞吐，不等于解决触摸手感。
`chunk_rows` 改完后 `flush_avg` 仍是 17ms（当时瓶颈在 PSRAM 带宽，不在送屏
分片）。原双缓冲配置已被 `CUSTOM_BUFFER` 取代（局部刷新模式下
`BUFFER_COUNT=0`）。

关于 `chunk_rows`：原值 24 把全屏 450 行切成 19 次
`setarea + DMA + sem_timedwait` 串行往返。LCDC 的 `LAYER0_SIZE.MAX_LINE`
位宽 11 bit（0x7FF = 2047 行），450 行远在硬件上限内，软件分片没有必要。

关于 `VSYNC_TIMER_PERIOD`：此前把它当成真正刷新节拍是错误的。它只负责发送
`LV_EVENT_VSYNC`；完整 `lv_nuttx_uv_init()` 的 FB 路径会删除 LVGL 默认刷新 timer，
并在 LCD fd 可写时直接调用 `_lv_display_refr_timer()`。本项目 LCD fd 永远报告可写，
所以该路径没有形成 16ms 帧率限制。当前实验排除 FB 路径，实际刷新周期重新由
`LV_DEF_REFR_PERIOD=16` 决定。

## 排障工具

| 脚本 / 程序 | 用途 |
|---|---|
| `tools/fps.ps1` | 抓串口 LCD 送屏调用日志和 `VELARIDE_PERF` 端到端手势日志（兼容旧 `LCD_FPS` 格式） |
| `apps/examples/epicbench/` | CPU vs EPIC 跑分，验证 EPIC 可用性 |
| `tools/wsl_sync_drivers.sh` | 同步 `drivers/` + `configs/` 到 WSL（`wsl_sync_board.sh` 只同步 `src/`） |
| `tools/nsh.ps1 -Cmd "free"` | 看堆余量（Umem 是 PSRAM 用户堆） |

改 defconfig 后必须 `wsl_build.sh clean` 全量重编，增量不会重跑 kconfig。
