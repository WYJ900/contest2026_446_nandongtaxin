# 显示链路迁移交接（2026-10-17 下午）

> **最新进度见 [晚间交接](HANDOFF_DISPLAY_20261017_EVENING.md)**（手表已是 rc8，横滑已修）。本页是迁移背景。

新对话接着做显示性能/迁移，先读本页，再按需查 [EPIC 实测交接](HANDOFF_EPIC_20261017.md)（第 11 节是旧链路 benchmark）。

## 1. 一句话现状

已把思澈官方显示链路（PSRAM 双整屏 framebuffer + 异步送屏）和 EPIC 接到我们的黄山派固件上，**手表现在装的就是迁移版 rc2**。
用户实测：整体明显更流畅，连续滑几分钟不再死机；**唯一明显问题是"左右横向滑动切表盘时画面稀碎"**，其他（上下滑、百叶窗、菜单、控制中心）都正常。
源码改动全在两个临时 git worktree 里，**没有进入正式工作区，也没有 commit**。

## 2. 当前状态（不要搞错）

| 项目 | 状态 |
| --- | --- |
| 手表固件 | `build/migrate/out/mig75_rc2.bin`，9,576,372 B，SHA-256 `2a1eda2a579c58f257dbd64999b938bc19b61b19a0c790dd023bc6859132af7f`。**不是 U7** |
| 回退镜像 | U7：`my-contest-repo/artifacts/velaride-ui-u7-20261011.bin`（SHA-256 `680936AF…20E7`） |
| 配置分区 | 测试前备份 `backup/ourbench-20261017/config_partition_0x129A0000.bin`（`4ff5794f…3652`）。rc2 运行后未回读核对；最新骑行仍为第 11 条 |
| 正式源码工作区 | `vendor/sifli`、`apps/graphics/lvgl/lvgl`、`apps`、`apps/examples/velaride`：**未改动** |
| WSL 正式树 `/home/wyj/velaride_ai` | 未改动（仍是 U7 源码） |
| 迁移源码 | `build/migrate/vendor_sifli`、`build/migrate/lvgl`：两个 detached worktree，**处于 merge 未提交状态**（`MERGE_HEAD` 分别为官方 PR `5c02633`、`16a970d18`） |
| 本地状态快照 | `vendor/sifli` 与 `apps/graphics/lvgl/lvgl` 各新增 `refs/velaride/local-20261017`（迁移前未提交改动的 stash 提交），worktree 基于它 |
| WSL 迁移树 | `/home/wyj/velaride_mig`（= U7 源码 + 两个 worktree 覆盖 + apps 一行 CMake），当前为正式开机脚本、75 行缓冲、不含 benchmark |

## 3. 实测结果

### 3.1 真实界面（rc2，用户手动滑动后 `velaride nav-stats`）

| 项目 | U7（旧链路） | 迁移版 rc2 |
| --- | --- | --- |
| 表盘滑动 slide | 约 45 Hz | 平均 48.2 Hz，滑动中逐秒 58–62 Hz（`VELARIDE_PERF`），max_gap 60 ms，625 帧中 slow 20 |
| 百叶窗 blinds | — | 43.9 Hz，max_gap 65 ms |
| 蜂窝菜单 | 约 20 Hz | 38.2 Hz |
| 列表菜单 | 约 20 Hz | 36.2 Hz |

### 3.2 LVGL benchmark（同 `bench_capture.py`，17 个双方共有场景平均）

| 链路 | 平均 FPS |
| --- | ---: |
| 我们旧链路（U7 配置，CPU） | 23.8 |
| 迁移版（75 行，VelaRide EPIC 策略，1 ms 节拍） | 36.8 |
| 官方固件（152 行，官方 EPIC 策略） | 42.1 |

迁移版逐项（`build/migrate/out/bench_mig75_c.log`）：空屏 51，大图平移 37，矩形 57/57，RGB/ARGB 图 49/46，旋转 35，标签 55，整屏文字 13，弧 53，span 29，
containers 27 / overlay 32 / opa 26 / opa_layer 19 / scrolling 24，widgets 15。与官方差距主要在大图平移、旋转、整屏文字，推测来自 75 行（官方 152 行）和调试选项全开（`CONFIG_DEBUG_FEATURES/ASSERTIONS/INFO`），未单独验证。

调优过程中的关键节点（都在同一 75 行配置上）：原样接入 11 FPS → 关掉 LCD 驱动逐块 `LOG_D` 后 21 → `CONFIG_USEC_PER_TICK` 10000→1000 后 32（全部场景平均，含无效项）。

## 4. 改了什么（全部在 worktree / WSL 迁移树）

### 4.1 vendor/sifli（`build/migrate/vendor_sifli`，合并 vendor_sifli#31）

- 冲突处理：`sifli_allocateheap.c` 用官方 `SRAM_*` 定义 + 保留我们"顶部 1280 B 保留"注释；`sf32lb_flash.c` **整份保留我们的版本**（官方 SFDP/DTR 改动未合入）；`co5300.c` 改包含 `drv_lcd.h`、其余保留我们的诊断/调光函数；`sf32lb_lcd.c/.h` 按官方删除。
- 黄山派板级（`boards/sf32lb52/lckfb_huangshan_pi/`）：`ld.script` 加 `.psram_bss`；`CMakeLists.txt` 加 SiFli LVGL 钩子；`src/CMakeLists.txt` 加 `lv_lcd.c`；`Kconfig` 加 `LV_USE_SIFLI_EPIC_IMAGE_SRAM_STAGE`；`bsp_init.c` 取官方版（XIP 96 MHz）。
- **启动卡在 `SFBL / AB` 的修复**：`bsp_init.c` 中 MPI2 分频与 FLASH2 时钟源切换改到 `.ramfunc` 的 `bsp_flash2_clock_switch()` 执行（原代码在 XIP 上边取指边切时钟）。之后约 6 次复位均正常启动；根因属推断。
- `drv_lcd.c` 移植 VelaRide 接口（都走 LCD 任务的同步 `LCD_MSG_CONTROL`）：`SETPOWER`（只开关面板显示，不重走 270 ms 初始化）、`CHECKSTATE`、`REINITIALIZE`（掉电重初始化 + 恢复亮度 + 计数）、`DUMPSTATE`、`SET/GETBRIGHTNESS`（20–80）、`WAITAREA`（轮询 `GET_BUSY`）；`PUTAREA_ASYNC` 返回 `-ENOTTY`。
- `nuttx_lcd_close()` 不再关屏：velaride 会短暂打开/关闭 `/dev/lcd0` 做探测，原实现最后一次 close 会给面板断电。
- `drv_lcd_nuttx.h`：`LOG_D` 只在 `CONFIG_DEBUG_LCD_INFO` 下编译（逐块日志在 1 Mbaud 串口上每帧几十毫秒）。
- **死机修复**：`drv_ext_dma.c`、`drv_lcd_fb.c` 中 EXTDMA / GP_DMA 中断优先级 0→6。芯片 3 位优先级，NuttX 用 BASEPRI 0x80 屏蔽；优先级 0 不受临界区约束，回调里 `nxsem_post` 会打断节拍中断里的信号量超时处理，表现为 `sem_waitirq.c:137` 断言（开机第一帧和滑动时各复现一次，现场一致）。改后用户连续滑几分钟未再出现。
- `lv_lcd.c`：自定义初始化补建触摸输入（velaride 依赖 `result.indev`）；每块 SRAM 绘制缓冲后加一行保护区，越界时打 `LV_LCD_GUARD`（rc2 浸泡中未出现）。

### 4.2 LVGL（`build/migrate/lvgl`，合并 apps_graphics_lvgl#41）

- `Kconfig` 冲突：保留旧 `LV_USE_DRAW_EPIC`（保持关闭）+ 官方新选项。
- `lv_nuttx_libuv.c`：`LV_USE_NUTTX_CUSTOM_INIT` 下跳过 fb poll（官方把 fd 本身当 driver_data，libuv 当指针解引用导致硬故障；LCD 设备也不可 poll）。
- EPIC 取舍（`lv_draw_sifli_epic.c`）：文字默认走 CPU（仅 `CONFIG_VELARIDE_EPIC_LABEL` 时启用，同时绕开"只检查首字形"的漏画问题）；无变换、不透明、小于 128×128 的图走 CPU；被裁剪的渐变填充走 CPU（官方会在每个分块重新起算渐变）。
- `lv_draw_sifli_epic_img.c`：解码出的遮罩在交给 EPIC 前刷 cache（PR 评审意见之一）。
- 运行时开关：环境变量 `LV_EPIC_OFF` 存在时所有任务走软件（进程首次评估时读取一次）。

### 4.3 配置（`setup_mig.sh` 写入 defconfig）

`CONFIG_USEC_PER_TICK=1000`、`BSP_USING_EPIC`、`BSP_USING_LCD_FRAMEBUFFER`、`LCD_FB_USING_TWO_UNCOMPRESSED`、`LV_FB_TWO_NOT_SCREEN_SIZE`、`LV_FB_LINE_NUM=75`、
`LV_USE_NUTTX_CUSTOM_INIT`、`LV_USE_SIFLI_EPIC`、`LV_USE_SIFLI_EPIC_IMAGE_SRAM_STAGE`，关闭 `LCD_FRAMEBUFFER`；去掉旧 `LV_NUTTX_LCD_*` 60 行缓冲。`apps/graphics/lvgl/CMakeLists.txt` 加 `target_link_libraries(lvgl PRIVATE sifli_hal)`（nuttx-apps#121）。
1 ms 节拍下 32 位节拍 49.7 天回绕，计时修复已用 64 位，无 71 分钟类问题。

### 4.4 资源

SRAM 484,392 / 523,008 B（剩约 38 KB）：绘制缓冲 2×58.5 KB（75 行 + 1 行保护），EPIC 图片暂存 30 KB；PSRAM 静态 702,080 B（两张整屏 fb）。

## 5. 待解决：左右横向滑动画面稀碎（下一步首要任务）

用户描述：左右横移滑动时"画面直接稀碎"，上下滑、百叶窗、菜单等都正常。**原因未定位**，以下是已知线索：

- `velaride_shell.c` 中上下滑和百叶窗的已揭开部分来自快照：`nav_flush_start_cb` 在 `LV_EVENT_FLUSH_START` 里用 CPU 把 PSRAM 快照逐行 `memcpy` 进当前绘制缓冲；`nav_slide_shift()` 对 `AXIS_H` 恒为 0，横向 slide 的快照偏移逻辑不同于纵向。
- 新链路的差异：`lv_lcd.c` 的 `render_start` 每帧把整屏标记无效（"避免 PSRAM→PSRAM 拷贝"）；`rounder_cb` 按对齐扩展区域；`lcd_flush` 用 `layer_head->buf_area` 作源区域；绘制缓冲之后由 EXTDMA 拷进 PSRAM fb。
- 横向移动时 LVGL 在渲染横向位移、被左右裁剪的对象，EPIC 的 IMAGE / FILL / LAYER 路径（尤其 x 方向裁剪、非对齐起点）是重点嫌疑；也可能是 CPU 写入绘制缓冲后 cache 未清就被 EXTDMA 读走。

建议排查顺序：
1. **先 A/B 关 EPIC**：在 WSL 迁移树的 rcS 里 `velaride &` 之前加 `set LV_EPIC_OFF 1`（`bench_mode.sh` 会覆盖 rcS，改完直接 `build_mig.sh`），烧录让用户横滑。正常 → EPIC 路径问题，再按任务类型逐个放回定位；仍碎 → 显示链路 / `nav_flush_start_cb` 问题。
2. 让用户拍一张横滑时的照片或描述碎的形态（错位条带 / 块状旧画面 / 颜色错），能直接区分"分块拼接错"与"cache 旧数据"。
3. 若是 cache：在 `nav_flush_start_cb` 写完后对写入区域 `up_clean_dcache`，或检查 `lcd_flush` 前是否已清 cache。

## 6. 尚未验证（不能宣称完成）

- `velaride lcd-diagnose` 在新驱动上的输出：rc2 上发命令后，在 4 秒探测窗口里没看到 `CO5300: reg=…`/`VELARIDE_LCD:` 行，需确认是否输出、输出到哪。
- 黑屏自动检测/恢复（`CHECKSTATE` / `REINITIALIZE`）：接口已移植，未做受控故障复现。
- 自动调光、抬腕亮屏/熄屏在新驱动下的实物表现；BLE 网页连接、校时、AI 复盘回传；长时间运行（只做过几分钟手动滑动）；ADC 问题照旧。
- 官方 PR 未处理的评审点：EPIC 中断挂接失败未回滚（我们未改）、draw-thread Kconfig（我们未启用）。
- 两个 worktree 的改动尚未整理成补丁进团队仓 `my-contest-repo`。

## 7. 复现与工具（`build/migrate/`）

| 文件 | 用途 |
| --- | --- |
| `setup_mig.sh [行数] [bench\|product]` | **全量重建** `velaride_mig`（rsync U7 树 + worktree 覆盖 + 写配置）。会清掉 WSL 树里的手改；之后需删 `cmake_out` 全量编译 |
| `rebuild75.sh` / `rebuild_prod.sh 75` | setup + 开机脚本 + 全量编译（benchmark 版 / 正式版），约 10 分钟 |
| `inc.sh <名> <worktree相对路径>...` | 把 worktree 里改过的文件拷进 WSL 树并增量编译，产物 `out/<名>.bin` |
| `build_mig.sh <名>` | 只编译；打印配置、内存、缓冲符号，检查镜像 < `0x990000` |
| `bench_mode.sh bench\|product` | 切换开机脚本（bench 只留 NSH） |
| `serial_log.py --reset --minutes N --log …` | 持续录串口，见断言/硬故障后多录 8 秒退出（抓死机现场用） |
| `a2l.sh <地址>...` | 用 WSL 迁移树 ELF 解析崩溃地址 |
| `port_*.py` | 已执行过的改动脚本，**不要重复执行**（会因匹配不到而断言失败） |

```sh
# 增量编译（WSL）
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/build/migrate/inc.sh mig75_rc3 lvgl/src/... vendor_sifli/boards/...
# 烧录 / 回退 U7（Windows 工作区根目录）
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "build/migrate/out/mig75_rc2.bin@0x12010000"
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "my-contest-repo/artifacts/velaride-ui-u7-20261011.bin@0x12010000"
# benchmark（需 bench 版固件）
py -3.13 -X utf8 build/epic_official/bench_capture.py --port COM5 --no-epicctl --log build/migrate/out/<名>.log
```

## 8. 本次踩的坑

- `sftool` 返回 101：COM5 被残留的采集脚本占用；先结束后台 `py`/`python` 进程再烧。
- 官方方案 `ai_agent` 先启动时，第一帧送屏就会触发上述 DMA 优先级断言（概率问题，不是启动顺序本身导致）；当前开机顺序已恢复与 U7 一致。
- U7 阶段写的 `velaride_shell.c` 依赖旧驱动"同步 ioctl"语义；新驱动所有控制都排进 LCD 任务队列，加接口时必须用同步消息，否则会在送屏中途碰总线。
- Git Bash heredoc 里写含引号的长 Python 会被 shell 截断；改用 `Write` 写 `.py` 文件再执行。
- `velaride_shell.c` 是 CRLF 文件，修改时按字节处理，见 [EPIC 实测交接](HANDOFF_EPIC_20261017.md) 第 10 节。

## 9. 横滑稀碎：定位与修复（rc3，待用户实物确认）

- **根因**：双 PSRAM framebuffer 交替，正在画的那张里是"上上帧"。官方 `lv_lcd.c` 在 `LV_EVENT_REFR_START` 里"有脏区就整屏标脏"来补齐，但 velaride 的 `nav_refr_start_cb` 也挂在 `REFR_START`，且注册在后，它标的脏区落在官方检查之后 → 那一帧只画转场改动区。
  横向擦除/百叶窗每帧只标"本帧新增"的窄条，上一帧那条在另一张 fb 里始终缺失 → 新旧画面条带交错（"稀碎"）。竖滑每帧标整块已揭开区域，恰好覆盖，所以正常。layout 更新产生的脏区同样受影响。
- **修复**（`vendor_sifli/.../lcd/lv_lcd.c`）：`render_start` 改挂 `LV_EVENT_RENDER_START`（所有标脏、layout、join 之后），重画"本帧脏区 ∪ 上一帧脏区"并按 LVGL 同规则合并；超过 `LV_INV_BUF_SIZE` 退化为整屏。比官方整屏重画更省。
  环境变量 `LV_FB_FULL` 存在时恢复整屏重画（A/B 用）。
- 固件 `build/migrate/out/mig75_rc3.bin`，SHA-256 `759bd941d527cbff0c518dfedf92d8c7c51afebdab3faa51062c7a495c66a4d7`，已烧录、开机正常；SRAM 486,500 B。

## 10. 横滑稀碎：真正根因（rc4–rc8）

A/B 结果（均为手表实测，日志在 `build/migrate/out/rc4_*` / `rc6_check.log` / `rc7_fullwin.log`）：

| 设置 | 横滑 |
| --- | --- |
| rc3（重画本帧 ∪ 上帧脏区） | 好一点，仍碎 |
| 关 EPIC | 仍碎 → 与 EPIC 无关 |
| 每帧整屏重画 | 正常，但拖动掉到 21–25 Hz |
| `LV_FB_CHECK` 比较两张 fb | 整段拖动**零不一致** → fb 内容正确，rc3 同步逻辑没问题 |
| 局部重画 + **整屏送屏** | **正常，50–60 Hz** |

结论：**窄窗口（横滑时的竖条）送到 CO5300 后显示错乱**，是屏的列窗口处理还是 LCDC ROI 尚未细分，未深究。
修复（rc8，`drv_lcd_fb.c`）：`fb_clip` 一律扩成整行宽（行仍是局部）。

运行时诊断开关（NSH，`/data` 是 tmpfs，重启即清）：`echo 1 > /data/lv_fb_full` 整屏重画、`/data/lv_epic_off` 关 EPIC、`/data/lv_fb_check` 双 fb 自检（不一致时打 `LV_FB_CHECK`）、`/data/lv_fb_fullwin` 整屏送屏。用 `build/migrate/nsh_cmd.py` 发命令。

另发现：`CONFIG_LCD_CO5300_VSYNC_ENABLE=y` 从未生效（`co5300.c` 检查的是不带 `CONFIG_` 的宏名，`sfconfig.h` 没映射），**TE 防撕裂一直是关的**（U7 也一样）。修复存在 `build/migrate/te_sfconfig.patch`，未实测（模组若没引出 TE 可能卡住送屏）。

上下滑优化（rc8，`build/migrate/velaride/velaride_shell.c`，是 app 源码的副本，正式工作区未动；`inc.sh` 已支持 `velaride/…`）：reveal 层在 `LV_EVENT_COVER_CHECK` 里对"会被快照整块覆盖"的区域返回 COVER，LVGL 不再渲染底下的表盘。`VELARIDE_PERF` 行增加 `axis=h/v`。
