# EPIC 硬件加速实测交接（2026-10-17）

新对话做显示性能或 EPIC 相关工作，**先读 [显示迁移交接](HANDOFF_DISPLAY_MIGRATION_20261017.md)**（迁移已完成并装机），再读本页。本页取代旧的 `EPIC_PORT.md`、`EPIC_UPSTREAM.md`
（已移出文档目录，见第 7 节），以及 `PERF_FINDINGS.md` 中已删除的 EPIC 结论。

## 1. 一句话结论

在本板上实测思澈官方给 openvela 的 EPIC 方案：**LVGL benchmark 全场景平均，EPIC 开/关都是 43 FPS，
整体没有提升**。图像旋转、整屏大图平移、叠加合成提升很大（最高约 3 倍）；文字类和 BIN 图片类反而
慢 15–22%。官方显示链路本身（PSRAM 整屏 framebuffer + 异步送屏）在纯 CPU 下就有 43 FPS，
它可能才是更大的杠杆，但**我们自己的显示链路还没用同一 benchmark 测过**，不能直接比较。

## 2. 当前状态（不要搞错）

| 项目 | 状态 |
| --- | --- |
| 手表固件 | 已恢复 `artifacts/velaride-ui-u7-20261011.bin`（SHA-256 `680936AF…20E7`），读回 `reg 0a=9c / 0c=55`，`sent=8 done=8 recoveries=0`，最新骑行第 11 条 |
| 配置分区 | 测试前后逐字节一致（SHA-256 `4ff5794f…3652`），备份在 `backup/epic-test-20261017/config_partition_0x129A0000.bin` |
| 本项目 EPIC | 仍关闭：`# CONFIG_LV_USE_DRAW_EPIC is not set`，ELF 中无 EPIC 符号；CPU 绘制，SRAM 2×60 行静态缓冲（`g_lcd_draw_buf` 93,600 B） |
| 本项目自研 EPIC 移植 | `vendor/sifli/chips/drivers/epic/`、`apps/graphics/lvgl/lvgl/src/draw/sf32lb_epic/`。**计划被官方实现取代，不要在它上面继续修**；其中关于"跨分块边界花屏"的注释未查证，很可能是自研移植缺 HAL 修复所致 |
| 源码工作区 | 未改功能代码。只改了文档、`velaride_shell.c` 一行注释（两份同步，保持 CRLF）、`epicbench.c` 注释 |

## 3. 官方方案是什么

思澈工程师（yunlonguu / zhenpeng）提交给 openvela `dev-ai-contest-2026` 的三个 PR，**目前均未合并**：

| PR | 内容 | 本地引用 |
| --- | --- | --- |
| [vendor_sifli#31](https://github.com/open-vela/vendor_sifli/pull/31) | 整套新 LCD/FB 驱动（`drv_lcd.c`、`drv_lcd_fb.c`、`lv_lcd.c`、删除 `sf32lb_lcd.c`）、EPIC 芯片层 `sf32lb52_epic.c`（cache 一致性、超时恢复）、HAL 修复 backport、PSRAM 堆布局、Flash SFDP、链接脚本 `.psram_bss` | `vendor/sifli` 的 `refs/pr/vendor31`（`5c02633`） |
| [apps_graphics_lvgl#41](https://github.com/open-vela/apps_graphics_lvgl/pull/41) | LVGL 9.1 draw unit `src/draw/sifli/epic/`：FILL/BORDER/LABEL/IMAGE/LAYER，含旋转缩放 | `apps/graphics/lvgl/lvgl` 的 `refs/pr/lvgl41`（`16a970d18`） |
| [nuttx-apps#121](https://github.com/open-vela/nuttx-apps/pull/121) | LVGL 链接 `sifli_hal`（4 行） | `apps` 的 `refs/pr/apps121`（`548ce62c6`） |

- 基线正好是我们 vendor（`af6f365`）与 LVGL（`0f2a49f58`）当前 HEAD，可干净叠加。
- 针对板级配置 `sf32lb52_lchspi_ulp`（就是黄山派在思澈 SDK 中的名字），不是我们的 `lckfb_huangshan_pi`。
- 关键配置：`CONFIG_BSP_USING_EPIC=y`、`CONFIG_LV_USE_SIFLI_EPIC=y`、`CONFIG_LV_USE_SIFLI_EPIC_IMAGE_SRAM_STAGE=y`、
  `CONFIG_BSP_USING_LCD_FRAMEBUFFER=y`、`CONFIG_LCD_FB_USING_TWO_UNCOMPRESSED=y`、`CONFIG_LV_FB_TWO_NOT_SCREEN_SIZE=y`、
  `CONFIG_LV_FB_LINE_NUM=152`、`CONFIG_LV_DEF_REFR_PERIOD=15`、`CONFIG_LV_USE_OS=0`。
- 内存：PSRAM 静态 702,080 B（两张 390×450 RGB565 扫描 framebuffer）；SRAM 用 365,884 B（我们当前约 440 KB）。
- 官方自带文档在 PR 中：`boards/sf32lb52/sf32lb52_lchspi_ulp/docs/{lvgl_epic_acceleration,lvgl_display_benchmark,lvgl_display_optimization_plan}.md`，
  用 `git -C vendor/sifli show refs/pr/vendor31:<路径>` 阅读。

## 4. 实测数据

方法：同一份官方固件（加了运行时开关），NSH 先 `set LV_EPIC_OFF 1` 跑一轮纯 CPU，再复位不设开关跑一轮
EPIC。显示链路完全相同，只差 EPIC。LVGL 9.1 官方 `lvgldemo benchmark`。

| 场景 | 纯 CPU FPS | EPIC FPS | 变化 |
| --- | ---: | ---: | --- |
| Empty screen | 55 | 55 | 0 |
| Moving wallpaper（整屏大图平移） | 43 | **60** | +40% |
| Single / Multiple rectangles | 62 / 62 | 62 / 62 | 0 |
| Multiple RGB images | 62 | 58 | −6% |
| Multiple ARGB images | 52 | 57 | +10% |
| Rotated ARGB images | 13 | **41** | ×3.2 |
| Multiple labels | 62 | 53 | −15% |
| Screen sized text | 20 | 16 | −20% |
| Multiple arcs | 61 | 60 | 0 |
| Span text | 38 | 31 | −18% |
| BIN image / I8 / A8 | 55 / 52 / 62 | 43 / 40 / 61 | −22% / −23% / 0 |
| Containers | 26 | 28 | +8% |
| Containers with overlay | 32 | **39** | +22% |
| Containers with opa / opa_layer | 28 / 22 | 27 / 21 | ≈0 |
| Containers with scrolling | 34 | 29 | −15% |
| Widgets demo | 19 | 17 | −10% |
| **All scenes avg.** | **43**（CPU 77%） | **43**（CPU 81%） | 0 |

另有一次未加开关的 EPIC 原版运行，结果与上表 EPIC 列一致（平均 43）。全程无断言、硬故障或超时。
原始日志：`build/epic_official/out/bench_ab_off.log`、`bench_ab_on.log`、`bench_epic_run1.log`。

解读：单步 EPIC 每次提交约 200 µs 固定开销（旧 epicbench 实测 100×100 fill CPU 70 µs / EPIC 193 µs，这个数据仍然有效）。
重活（旋转、缩放、大面积 alpha 合成）硬件远胜 CPU；文字按字形逐个提交，开销吃掉收益。

## 5. 已作废的旧结论

| 旧说法 | 现状 |
| --- | --- |
| EPIC 是 DMA master，访问不到 PSRAM | **作废**。旧失败与 `hezip` 空指针同时存在，无法区分原因；官方文档称 505×505 PSRAM 旋转逐像素通过（需提交前 clean、完成后 invalidate）。**本板尚未直接验证**：PR 最终版已删除 `epicctl`，本次 benchmark 中 EPIC 实际读写的多是 SRAM/XIP |
| PSRAM 快照因此不能用 EPIC，转场只能 CPU 按行拷贝 | 前提作废，需要重新评估 |
| 文字/图片跨 80 行分块边界会花屏，必须关 EPIC 文字/图片 | 只在自研移植上出现；官方实现补了 `MAX_COL/MAX_LINE = width-1/height-1` 等 HAL 修复，benchmark 无此问题 |
| 关 EPIC 修好黑屏 | 早已否定（见 `DISPLAY_RECOVERY_20261009.md`），与本次无关 |

## 6. 官方方案的已知问题（采用前必须处理）

1. **早期启动对链接布局敏感**：只把 `CONFIG_LV_USE_SIFLI_EPIC` 关掉重编的固件（`build/epic_official/out/official_nsh_cpu.bin`，
   SHA-256 `b654e2f4…cea8`），每次启动都停在串口 `SFBL / AB`，即 `__start` 中 `HAL_Init()`（时钟、DLL2、PSRAM 初始化）之后没打出 `C`。
   与能启动的版本相比，早期启动函数都在 XIP flash（`0x1201xxxx`），只偏移几十字节。推测是时钟/Flash 切换期间从 XIP 取指的竞态，
   **未定位**。复现：烧该 bin，RTS 复位。
2. PR 文档提到的 `epicctl`、`lcdstat` 命令在最终版中已删除（commit 说明 "remove benchmark-only helpers"）。
3. PR 仍是 Open，有 Copilot 评审的 5 个未处理点（NuttX 下绘制线程选项、中断挂载失败未处理、标签需扫描全部字形、裁剪区渐变偏移、遮罩未刷 cache）。
4. 官方显示链路替换了我们的 `sf32lb_lcd.c`。我们的黑屏检测/恢复（`lcd-diagnose`/`lcd-recover`、帧间寄存器检查）、调光、异步送屏改动都在旧驱动上，迁移要逐项移植。
5. 官方 SRAM 方案（2×152 行 ≈ 237 KB）在我们的镜像里放不下（我们 SRAM 剩余约 80 KB），参数要重新设计。

## 7. 文档清理记录

- `EPIC_PORT.md`、`EPIC_UPSTREAM.md`（`project-docs/` 与 `my-contest-repo/docs/development-notes/` 各一份）已移到
  `backup/epic-test-20261017/superseded-docs/`。团队仓那两份在 git 中显示为删除。
- `PERF_FINDINGS.md`、`NAVIGATION_ARCHITECTURE.md`、`README.md`（两套目录同步）删除了 PSRAM 不可达结论和自研 A/B 段落，改为指向本页。
- 修改前的两套文档完整备份：`backup/epic-test-20261017/docs-before/`。
- 代码注释：`apps/examples/velaride/velaride_shell.c`（与 `my-contest-repo/app/velaride/` 同步）、`apps/examples/epicbench/epicbench.c`。

## 8. 建议下一步（按顺序）

1. **用同一 benchmark 测我们自己的显示链路**（2026-10-17 已完成，结果见第 11 节：约为官方链路的 58%）。
   做法：复制一份 WSL 构建树（不要动 `/home/wyj/velaride_ai`），在 `lckfb_huangshan_pi` defconfig 里加
   `CONFIG_EXAMPLES_LVGLDEMO=y`、`CONFIG_LV_USE_DEMO_BENCHMARK=y`、`CONFIG_LV_USE_SYSMON=y`、`CONFIG_LV_USE_PERF_MONITOR=y`，
   并从 `src/etc/init.d/rcS` 去掉 `velaride &`（否则抢屏）。镜像必须小于 `0x990000`，烧前先备份配置分区，测完烧回 U7 并核对哈希。
2. 若我们的链路明显低于 43 FPS：迁移到官方显示链路 + EPIC，先解决第 6 节问题 1 和 5。
3. EPIC 策略：**只让 IMAGE/LAYER（转场快照、旋转缩放）走 EPIC，LABEL 留 CPU**，在官方 `_epic_evaluate()` 里按任务类型取舍，取两者之长。
4. 转场/菜单要受益，必须改为用 LVGL 图像/图层绘制快照（现在是 `nav_flush_start_cb` 按行直拷，用不到 EPIC），并在本板直接验证 EPIC 读 PSRAM 快照。
5. 每一步保留可回退镜像，用 `velaride nav-stats` 对比转场/菜单帧率。

## 9. 测试环境与复现

| 内容 | 位置 |
| --- | --- |
| 官方 PR 快照 tar、脚本、日志、固件 | `build/epic_official/`（`setup_tree.sh`、`build_one.sh`、`add_toggle.sh`、`bench_capture.py`、`out/`） |
| WSL 验证树（官方 PR + 运行时开关） | `/home/wyj/epic_official`，产物 `cmake_out/lchspi_nsh{,_cpu}/` |
| 可启动的官方 EPIC 固件（带 `LV_EPIC_OFF` 开关） | `build/epic_official/out/official_nsh.bin`，SHA-256 `1f73bce7…37f3` |

```sh
# 编译（WSL 内）
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/build/epic_official/build_one.sh nsh
# 备份配置分区（注意 read_flash 语法是 文件@地址:长度）
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 read_flash "backup/<目录>/config.bin@0x129A0000:0x400000"
# 烧录官方固件 / 跑 benchmark（--epic-off 为纯 CPU）
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "build/epic_official/out/official_nsh.bin@0x12010000"
py -3.13 -X utf8 build/epic_official/bench_capture.py --port COM5 --no-epicctl [--epic-off] --log build/epic_official/out/<名>.log
# 恢复 U7
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "my-contest-repo/artifacts/velaride-ui-u7-20261011.bin@0x12010000"
```

官方固件只把 `0x129A0000` 起的 4 MB 注册为 `/dev/config0`，不挂载、不格式化；本次实测配置分区未被改动。

## 10. 本次踩的坑

- Git Bash 的 `sed -i` 会把 CRLF 文件整体改成 LF（`velaride_shell.c` 是 CRLF）。改 CRLF 文件用 Python 按字节处理，改完用 `file` 核对。
- Windows 下 `python3` 可能是商店占位程序，会卡住；一律用 `py -3.13`。
- `wsl.exe bash -c '...$VAR...'` 里的变量会被外层展开成空；把命令写成 `.sh` 文件（去掉 `\r`）再用 `wsl.exe ... bash /mnt/d/...` 执行。
- U7 复位后马上发命令会被 AI Agent 的 `vela>` 命令行接走（`Unknown command: velaride`）；等启动完成后不带 `--reset` 再发。
- `vendor/sifli`、`apps/graphics/lvgl/lvgl`、`apps` 三个仓库新增了只读引用 `refs/pr/*`，不影响工作区；不需要时 `git update-ref -d refs/pr/<名>` 删除。
## 11. 我们自己显示链路的 benchmark（2026-10-17 实测）

方法：复制 WSL 树到 `/home/wyj/velaride_bench`（原树未动），U7 配置上只加 `EXAMPLES_LVGLDEMO`、`LV_USE_DEMO_BENCHMARK`、
`LV_USE_DEMO_WIDGETS`、`LV_USE_SYSMON`、`LV_USE_PERF_MONITOR`；rcS 去掉 `ai_agent`/`velaride`/`velaride_ble`（与官方测试同为无后台任务）。
显示参数与 U7 一致：CPU 绘制，SRAM 2×60 行，`LV_DEF_REFR_PERIOD=16`（官方 15）。固件 `build/ourbench/out/ourbench.bin`
（9,790,648 B，SHA-256 `fbf89068…f689`），日志 `build/ourbench/out/bench_ours_run1.log`，脚本 `build/ourbench/`。

| 场景 | 我们（CPU） | 官方链路（CPU） | 官方链路（EPIC） |
| --- | ---: | ---: | ---: |
| Empty screen | 36 | 55 | 55 |
| Moving wallpaper | 15 | 43 | 60 |
| Single / Multiple rectangles | 33 / 50 | 62 / 62 | 62 / 62 |
| Multiple RGB / ARGB images | 36 / 24 | 62 / 52 | 58 / 57 |
| Rotated ARGB images | 5 | 13 | 41 |
| Multiple labels | 45 | 62 | 53 |
| Screen sized text | 9 | 20 | 16 |
| Multiple arcs | 35 | 61 | 60 |
| Span text | 23 | 38 | 31 |
| Containers | 26 | 26 | 28 |
| Containers with overlay | 15 | 32 | 39 |
| Containers with opa / opa_layer | 24 / 9 | 28 / 22 | 27 / 21 |
| Containers with scrolling | 11 | 34 | 29 |
| Widgets demo | 9 | 19 | 17 |
| **17 个共有场景平均** | **23.8** | **40.6** | — |

注意：
- 我们固件的 "All scenes avg." 显示 24，但含 4 个 FreeType 场景（我们开了 FreeType、官方没开；TTF 文件不存在，退回默认字体）和
  4 个 BIN/PNG 场景（资源路径 `A:` 盘符未注册，图片没加载，3 FPS 无意义），这 8 项不参与对比。
- 我们这边 sysmon 的 CPU% 只有 0–22%，与 57 ms 的渲染时间矛盾，推测在 uv 循环下空闲统计不准，不要引用。
  60 行分块送屏时，等待上一块传输完成的时间很可能计在 render time 里，因此瓶颈更像是"分块 + 同步送屏"，不是绘制本身。
- 结论：**同样纯 CPU 绘制，官方显示链路（PSRAM 整屏 framebuffer + 异步送屏）约快 1.7 倍**，整屏重绘场景（大图平移、滚动、
  opa_layer）差 2.4–3 倍。按"提升够大才重新设计显示架构"的原则，迁移显示链路已有数据支持；EPIC 是迁移后的第二步收益。
- 测试后已烧回 U7 并核对：固件区回读 SHA-256 `680936AF…20E7`，配置分区回读 `4ff5794f…3652`（与测试前备份
  `backup/ourbench-20261017/` 一致），`reg 0a=9c / 0c=55`，`sent=284 done=284 recoveries=0`，最新骑行第 11 条。
