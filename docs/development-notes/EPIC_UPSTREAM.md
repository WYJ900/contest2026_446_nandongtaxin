# 官方 EPIC / GPU 加速方案

**来源：[OpenSiFli/SiFli-SDK](https://github.com/OpenSiFli/SiFli-SDK)（Apache 2.0）**

本文记录从官方源码读到的事实。文件路径都是 SDK 仓库里的真实路径。

## 一句话

**官方已经写好了 LVGL v9 的 EPIC 绘制后端**，不需要自己实现。
路径 `middleware/lvgl/lv_drivers_v9/sifli/`。

## 官方 LVGL v9 EPIC 后端

```
middleware/lvgl/lv_drivers_v9/sifli/
├── lv_draw_epic.c          527 行   draw unit 注册 + evaluate/dispatch
├── lv_draw_epic.h
├── lv_draw_epic_fill.c     159 行   矩形填充
├── lv_draw_epic_img.c               图像绘制（含 transform）
├── lv_draw_epic_label.c             文字（需 EPIC_SUPPORT_A8）
├── lv_draw_epic_border.c            边框
├── lv_draw_epic_arc.c               圆弧
├── lv_epic_utils.c         309 行   调 drv_epic_* 的封装
└── lv_epic_utils.h
```

接口是标准 LVGL v9 draw unit，和我们用的 LVGL 版本对得上：

```c
void lv_draw_epic_init(void)
{
    lv_draw_epic_unit_t *u = lv_draw_create_unit(sizeof(lv_draw_epic_unit_t));
    u->base_unit.dispatch_cb = dispatch;
    u->base_unit.evaluate_cb = evaluate;
    ...
}
```

### 哪些绘制能走 EPIC

`evaluate()` 里的判断（`lv_draw_epic.c:153`）：

| 任务类型 | 走 EPIC 的条件 | preference_score |
|---|---|---|
| `FILL` | `radius == 0`（**圆角走不了**） | 70 |
| `BORDER` | `side == FULL` 且 `radius == 0` | 90 |
| `IMAGE` | 无 recolor，格式受支持；RGB565A8 + transform 除外 | 80 |
| `LAYER` | 同 IMAGE | 80 |
| `LABEL` | 需 `EPIC_SUPPORT_A8`；矢量字体（FreeType+ThorVG）走不了 | 95 |
| `LINE` / `ARC` | 源码里 `#if 0` 屏蔽了 | — |

**`LAYER` 和 `IMAGE` 带 transform 走硬件** —— swiper 滑动就是 LAYER 位移，
这是官方表盘丝滑的来源。

注意两条对本项目直接相关的限制：

- **圆角矩形走不了 EPIC**（`radius != 0` 直接 return 0）。我们卡片上的
  `border-radius` 会落回软渲染。
- **FreeType 矢量字体走不了 EPIC**。我们开了
  `CONFIG_LV_USE_FREETYPE` + `LV_USE_VECTOR_GRAPHIC`，中文文字仍是软渲染。

## 依赖链

后端不直接调 HAL，中间隔着一层驱动：

```
lv_draw_epic.c  →  drv_epic_fill_ext() / drv_epic_blend()   ← 驱动层
                →  HAL_EPIC_FillStart() / HAL_EPIC_BlendStart()   ← HAL 层
```

| 层 | 路径 | RT-Thread 依赖 |
|---|---|---|
| HAL | `drivers/hal/bf0_hal_epic.c` | **无**。本地那份 9 处 `rtthread` 引用全是注释状态，已实测直接可调 |
| 驱动 | `rtos/rtthread/bsp/sifli/drivers/drv_epic.c` | 705 行，**只有 2 处** `rt_err_t` 类型别名 |
| 驱动（单步） | `rtos/rtthread/bsp/sifli/drivers/drv_epic_single.c` | 1322 行，浅层依赖：信号量 / 中断进出 / tick 转换 / `rt_flash_*` |

`drv_epic_single.c` 的 RT-Thread API 用量：

```
23  rt_err_t                    类型别名
12  rt_flash_get_handle_by_addr 从 flash 读图源，特定路径才用
 5  rt_sprintf
 3  rt_interrupt_enter/leave    NuttX 有对应物
 3  rt_flash_lock/unlock
 2  rt_sem_release              → nxsem_post
 1  rt_sem_take / trytake / init
 1  rt_tick_from_millisecond
 1  rt_pm_request / release     电源管理，可空实现
```

都是可映射的，没有深绑 RT-Thread 内核的东西。

## 两种工作模式

官方文档 `docs/source/zh_CN/drivers/epic.md`。**两种模式不能混用**，编译期
menuconfig 选。

### 单步模式

```c
drv_gpu_open();
drv_epic_fill(...) / drv_epic_copy(...) / drv_epic_blend(...) / drv_epic_transform(...);
drv_gpu_close();
```

- 带 `cbk` 的接口是异步的，回调触发才算画完
- `drv_gpu_check_done(ms)` 等上一次完成，`drv_epic_is_busy()` 立即返回状态
- **只有单步模式允许用栈上/局部变量作输入 buffer**
- 55x 系列只支持这个模式

### Render list 模式

批量提交，一次中断处理多个操作 —— 这才是官方表盘用的模式。

```c
drv_gpu_open();
drv_epic_setup_render_buffer(&buf1, &buf2, buf_size);   // 两块等大 buffer
drv_epic_alloc_render_list(&virtual_render_buf, &ow_area);
// 每个操作：
drv_epic_alloc_op(); HAL_EPIC_LayerConfigInit(&o->mask);
// 填 op / clip_area / desc
drv_epic_commit_op(o);
// 组消息后提交
drv_epic_render_msg_commit(&msg);
```

两种消息 id：

- `EPIC_MSG_RENDER_DRAW` — 分块渲染 + 送屏，走 `partial_done_cb`。
  输出地址不会被真正写入，示例用 dummy `0xCCCCCCCC`
- `EPIC_MSG_RENDER_TO_BUF` — 渲染到 buffer，完成时单次 `done_cb`。
  需要真实地址（示例 `0x60000000`），支持缩放到目标尺寸

约束：

- render list 模式**不能用局部变量**做 buffer，因为批量提交期间
  「所有相关的内存引用都不能变动」
- 分块路径的像素对齐通过 `msg.content.rd.pixel_align` 传
- 图层顺序：index 0 在最底，往上叠。mask 层只作用于紧下方那一层，
  且格式必须是 A2/A4/A8 + `ax_mode = ALPHA_BLEND_MASK`
- **应用必须实现 `drv_gpu_is_cached_ram(start, len)`**，标记哪些区域跳过
  D-cache 清理，返回 0 = uncached，1 = cached
- 线 / 圆 / 多边形 / 圆弧**只有 render list 模式有**

## 官方 HAL 示例

`example/hal/epic/src/main.c`（240 行，纯 HAL 不经驱动层）。初始化顺序：

```c
L2_NON_RET_BSS_SECT_BEGIN(epic_buffers)
L2_NON_RET_BSS_SECT(epic_buffers, ALIGN(64) static uint8_t buffer0[INPUT_BUF_SIZE]);
L2_NON_RET_BSS_SECT_END                    // ← buffer 在 L2 SRAM，不是 PSRAM

mpu_dcache_clean(buffer0, INPUT_BUF_SIZE);

HAL_NVIC_SetPriority(EPIC_IRQn, 3, 0);
HAL_NVIC_EnableIRQ(EPIC_IRQn);

epic_handle.hezip = &ezip_handle;
epic_handle.hezip->Instance = EZIP;
HAL_EZIP_Init(epic_handle.hezip);          // ← EPIC 之前必须先 EZIP
epic_handle.Instance = hwp_epic;
HAL_EPIC_Init(&epic_handle);
```

我们的 `apps/examples/epicbench/` 就是照这个改的，已在真机跑通。
实测数据见 [PERF_FINDINGS.md](PERF_FINDINGS.md)。

## 版本差异

官方 SDK 里 LVGL 有两份：

- `external/lvgl_v8/src/draw/sifli_epic/lv_gpu_sifli_epic.h` — v8 时代的
- `middleware/lvgl/lv_drivers_v9/sifli/` — **v9 的，与我们版本匹配**

配置模板 `middleware/lvgl/lv_conf_sifli.h` 可以参照。

## 其它可用资源

| 路径 | 内容 |
|---|---|
| `example/rt_device/gpu/render_list_mode/` | render list 模式完整示例，带表盘素材（ezip / jpeg / 565A dat） |
| `docs/source/zh_CN/hal/epic.md` | HAL 层 API 文档 |
| `docs/source/zh_CN/api/hal/epic.md` | API 参考 |
| `drivers/hal_epic_ex/libhal_epic_ex_gcc.a` | EPIC 扩展预编译库（GCC 版） |
| `drivers/cmsis/sf32lb52x/epic.h` | 寄存器定义 |

## 移植到 openvela 的工作量

要用上官方后端，需要：

1. 把 `lv_drivers_v9/sifli/` 9 个文件搬进 `apps/graphics/lvgl/`
2. 移植 `drv_epic.c` + `drv_epic_single.c`（RT-Thread API 映射到 NuttX，
   依赖很浅）
3. 实现 `drv_gpu_is_cached_ram()`
4. 在 LVGL 初始化处调 `lv_draw_epic_init()`
5. **解决 buffer 位置问题** —— EPIC 访问不到 PSRAM，而 SRAM 只剩约 113KB，
   放不下 351KB 全屏 framebuffer。这是最大的未知项

第 5 条是关键：即使后端移植完，framebuffer 在 PSRAM 上 EPIC 就用不了。
可能要配合局部刷新（`LV_NUTTX_LCD_CUSTOM_BUFFER` + 小分块 buffer 放 SRAM）。
