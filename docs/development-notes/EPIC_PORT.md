# EPIC GPU 加速移植记录

**2026-08-28 起。目标：把官方 SiFli-SDK 的 LVGL v9 EPIC draw unit 移植到本工程
（openvela / NuttX / LVGL 9.1 fork），解决表盘滑动卡顿。**

状态：代码已写完，等待编译 + 真机验证。验证数据出来前，本文的性能预期都只是
预期，不是结论。

## 改了什么

### 新增文件

| 文件 | 内容 |
|---|---|
| `vendor/sifli/chips/drivers/epic/drv_epic.h` | 驱动公共 API（官方单步模式 API 的 NuttX 版） |
| `vendor/sifli/chips/drivers/epic/drv_epic_priv.h` | 驱动内部结构 |
| `vendor/sifli/chips/drivers/epic/drv_epic.c` | 官方 drv_epic.c + drv_epic_single.c 的移植合并 |
| `apps/graphics/lvgl/lvgl/src/draw/sf32lb_epic/lv_draw_epic.{c,h}` | draw unit 注册 + evaluate/dispatch |
| `.../sf32lb_epic/lv_epic_utils.{c,h}` | 层配置公共代码 + 统一 blend 入口 |
| `.../sf32lb_epic/lv_draw_epic_fill.c` | 矩形填充 + 双向渐变 |
| `.../sf32lb_epic/lv_draw_epic_border.c` | 边框（直线段走 EPIC，圆角扫描线走软 mask） |
| `.../sf32lb_epic/lv_draw_epic_label.c` | A8 字形连续 blend（cont_blend 会话摊薄启动开销） |
| `.../sf32lb_epic/lv_draw_epic_img.c` | 图像/图层合成，含旋转缩放（官方表盘丝滑的来源） |

### 改动文件

| 文件 | 改动 |
|---|---|
| `vendor/sifli/chips/sf32lb52/CMakeLists.txt` | 编译 drv_epic.c（跟随 `CONFIG_LV_USE_DRAW_EPIC`） |
| `apps/graphics/lvgl/lvgl/Kconfig` | 新增 `LV_USE_DRAW_EPIC` |
| `apps/graphics/lvgl/lvgl/src/lv_init.c` | 注册 EPIC draw unit |
| `apps/graphics/lvgl/CMakeLists.txt` | EPIC 后端源码过滤 + vendor HAL 头文件路径 + SOC 宏 |
| `configs/nsh/defconfig` | `CONFIG_LV_USE_DRAW_EPIC=y`；`LCD_BUFFER_SIZE` 120→60 |

`LCD_BUFFER_SIZE` 改回 60 的原因：EPIC 访问不到 PSRAM，draw buffer 必须留在
SRAM。120 行 = 93.6KB，SRAM 堆只剩约 113KB，太挤，有落到 PSRAM 的风险；
60 行 = 46KB 是 PERF_FINDINGS.md 实测验证过在 SRAM 的配置。

## 关键设计

### RT-Thread → NuttX 映射（驱动层）

| 官方 | 本移植 |
|---|---|
| `rt_sem_*`（GPU 互斥） | `nxsem_*`，初值 1 |
| `EPIC_IRQHandler()` 直接进向量表 | `irq_attach(NX_IRQ(EPIC_IRQn), epic_isr)` + `up_enable_irq`（照 epicbench） |
| `mpu_dcache_clean/invalidate` | `up_clean_dcache` / `up_invalidate_dcache` |
| `rt_flash_lock/unlock` | 空实现。渲染路径没有并发 flash 擦写；日后图源在 flash 且有后台写入时要在 MTD 层补锁 |
| `rt_pm_request/release` | 空实现（未启用 PM） |
| `INIT_PRE_APP_EXPORT` | `drv_gpu_open()` 惰性初始化 |

`drv_gpu_is_cached_ram()`：只有 `>= PSRAM_BASE` 算 cached，与官方
`IS_DCACHED_RAM` 口径一致。SF32LB52 的 cache 只覆盖 XIP 外存，SRAM 不经 cache。

### LVGL 9.3 → 9.1 fork 适配（后端）

- 绘制函数签名 `lv_draw_task_t *` → `lv_draw_unit_t *`（目标层/裁剪区在 unit 上）
- `lv_area_intersect` → `_lv_area_intersect`；image helper 带下划线前缀
- task 没有 `target_layer` 字段 → evaluate 里用 `lv_draw_dsc_base_t::layer`
- 本 fork 的 `lv_draw_label_iterate_characters` 会预取字形位图到
  `glyph_data`（A8 draw buf），字形回调不用自己拿位图
- dispatch 收尾照本仓库 `lv_draw_vg_lite.c`：同步执行，置
  `LV_DRAW_TASK_STATE_READY`（不是 9.3 的 FINISHED）
- 无 EZIP/JPEG 图像 flag（fork 解码器不支持），压缩图源不走 EPIC

### PSRAM 防线（官方没有，本移植新增）

EPIC 是 DMA master，访问不到 PSRAM（实测 state 卡 BUSY、EOF_IRQ 恒 0）。
四道防线，全部落空才会把 PSRAM 地址交给硬件：

1. `evaluate()`：IMAGE 只收 `LV_IMAGE_SRC_VARIABLE` 且数据地址
   `< PSRAM_BASE`、非压缩；带 `bitmap_mask_src` 的一律不收
2. `dispatch()`：目标 layer buffer / LAYER 源 buffer 分配后再查一次，
   不可达就把任务改回软渲染单元（`preferred_draw_unit_id = 1`）
3. `lv_epic_draw_blend()`：src/mask/dest 任一不可达 → `lv_draw_sw_blend`
4. 字形回调：glyph draw buf 不可达 → 该字形走软 blend

### 哪些绘制走 EPIC（evaluate 通过条件）

| 任务 | 条件 | score |
|---|---|---|
| FILL | radius==0；渐变仅限 2 stop 横/竖 | 70 |
| BORDER | 四边完整 + radius==0 | 90 |
| LABEL | 非矢量字形（FreeType 位图模式可以走） | 95 |
| IMAGE/LAYER | 无 recolor/skew/mask；RGB565A8+transform 除外；变量源 | 80 |

圆角矩形、圆弧、线条仍走软渲染（与官方一致，官方 evaluate 里 LINE/ARC
也是 `#if 0`）。velaride 卡片全部 radius=0，主要 fill 都能上 GPU；
进度条 radius=3 走软渲染，面积小无所谓。

## 预期与风险

- 单次 EPIC 操作有约 200µs 固定开销（epicbench 实测），小面积 fill 可能比
  CPU 慢。真正的收益在 label 连续 blend、大面积 alpha 合成、图像旋转缩放。
  如果真机数据显示纯 fill 变慢，可以在 evaluate 里加面积阈值
  （如 <64×64 return 0）。
- `LV_USE_OS=0`，dispatch 同步执行 —— EPIC 跑的时候 CPU 在等。后续如果
  要 CPU/GPU 并行，需要开 LV_USE_OS 或用 render list 模式（官方表盘的
  完整形态，本次未移植）。
- 若字形 draw buf（`lv_draw_buf_create` → `malloc`）经常落 PSRAM，label
  会频繁走软回退，收益打折。真机上看 `VELARIDE_PERF` 对比就知道。

## 验证方法

```
# 编译（defconfig 变了，必须 clean）
bash tools/wsl_sync.sh && bash tools/wsl_build.sh clean
# 烧录
tools/flash.ps1
# 对比：跑 velaride，滑动卡片，看串口
#   VELARIDE_PERF: input=..Hz max_gap=..ms refresh=..Hz max_gap=..ms
# EPIC 生效标志：启动日志出现 "drv.epic: drv_gpu opened."
# 关掉 EPIC 对照：defconfig 去掉 CONFIG_LV_USE_DRAW_EPIC=y 重编
```
