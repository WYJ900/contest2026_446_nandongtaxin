# 显示链路交接（2026-10-17 晚）

背景和迁移细节见 [迁移交接](HANDOFF_DISPLAY_MIGRATION_20261017.md)（第 9、10 节是本次横滑排查的过程），旧链路 benchmark 见 [EPIC 实测交接](HANDOFF_EPIC_20261017.md)。

> **已过时**：rc9 → rc17 的进展（iris 残影、帧调度、诊断工具、手表现装 rc17）见 [深夜交接](HANDOFF_DISPLAY_20261017_NIGHT.md)。本页只作 rc3–rc8 的背景资料，第 2 节"当前状态"已不再准确。

## 1. 一句话现状

横滑稀碎已修好，用户确认。上下滑和百叶窗"很好"。**还剩两件事**：
1. 圆形扩散（iris）转场"还是有点问题"，具体是什么现象**还没问清**；
2. 设置里的列表菜单帧率偏低（实测 34.9 Hz）。

## 2. 当前状态（不要搞错）

| 项目 | 状态 |
| --- | --- |
| 手表固件 | `build/migrate/out/mig75_rc8.bin`，9,578,336 B，SHA-256 `cfbb06a3adfddf37ebe88ca751692b513ddcb13dad0d08c641487a2c48d02418`。**不是 U7，也不是 rc2** |
| 回退镜像 | U7：`my-contest-repo/artifacts/velaride-ui-u7-20261011.bin`；上一个稳定迁移版 rc2：`build/migrate/out/mig75_rc2.bin` |
| 正式源码工作区 | `vendor/sifli`、`apps/graphics/lvgl/lvgl`、`apps/examples/velaride`：**仍未改动** |
| 迁移源码 | `build/migrate/vendor_sifli`、`build/migrate/lvgl`：两个 detached worktree，merge 未提交 |
| velaride 应用改动 | `build/migrate/velaride/velaride_shell.c`：**普通副本，不是 git**，取自 WSL 迁移树（与正式工作区相比只差一行注释 + 本次改动） |
| WSL 迁移树 | `/home/wyj/velaride_mig`，= rc8 源码，正式开机脚本 |
| 未合入的补丁 | `build/migrate/te_sfconfig.patch`（开启 TE 防撕裂，未实测，见第 5 节） |

## 3. 本次改了什么（rc3 → rc8）

### 3.1 横滑稀碎（已修，用户确认）

**根因：窄窗口送到 CO5300 后显示错乱。** 横滑每帧只送一条窄竖条，正好撞上。是屏本身的列窗口问题还是 LCDC ROI 的问题，没有细分。

排除过程（都是实物 A/B）：关 EPIC 仍碎 → 每帧整屏重画不碎但只有 21–25 Hz → 双 fb 自检整段拖动**零不一致**（fb 内容是对的）→ 局部重画 + 整屏送屏不碎、50–60 Hz。

| 文件 | 改动 |
| --- | --- |
| `vendor_sifli/.../lcd/drv_lcd_fb.c` | **真正的修复**：`drv_lcd_fb_write_send` 里把 `fb_clip` 的 x 一律扩成整行宽（行仍是局部） |
| `vendor_sifli/.../lcd/lv_lcd.c` | `render_start` 从 `REFR_START` 改到 `RENDER_START`，重画"本帧 ∪ 上帧"脏区（替代官方"整屏标脏"）。这修的是另一个真实漏洞（velaride 的脏区标在官方检查之后），对应用户说的"好了一点" |
| 同上 | 运行时诊断开关，见 3.3 |
| `lvgl/src/draw/sifli/epic/lv_draw_sifli_epic.c` | `LV_EPIC_OFF` 改成全局 `lv_sifli_epic_off`，可在运行时切换 |

### 3.2 上下滑优化（已上，效果有限）

`velaride_shell.c`：新增 `reveal_covers()` / `reveal_cover_cb()`。reveal 层在 `LV_EVENT_COVER_CHECK` 里，对"`nav_flush_start_cb` 会用快照整块覆盖"的区域返回 COVER，LVGL 就不再渲染下面的表盘。判断条件和 `nav_flush_start_cb` 一一对应：`cover_preview` 关中心页时整屏都算；slide 和 blinds 只算已揭开的条带；iris 不算。
同时 `VELARIDE_PERF` 行加了 `axis=h/v`。

### 3.3 诊断开关（rc8 里都在）

NSH 里创建或删除文件即可，每 0.5 s 检查一次。`/data` 是 tmpfs，重启后自动全部关闭。

| 文件 | 作用 |
| --- | --- |
| `/data/lv_fb_full` | 每帧整屏重画 |
| `/data/lv_epic_off` | 全部绘制走 CPU |
| `/data/lv_fb_check` | 每帧比较两张 fb 在"上帧未重画区域"是否一致，不一致时打 `LV_FB_CHECK:`（很卡） |
| `/data/lv_fb_fullwin` | 每帧整屏送屏 |

发命令：`py -3.13 -X utf8 build/migrate/nsh_cmd.py --wait 2 "" "echo 1 > /data/lv_epic_off"`（第一个空串用来唤醒 NSH 提示符）。

## 4. rc8 实测数据（`build/migrate/out/rc8_test.log` + `velaride nav-stats`）

| 场景 | 平均 | 备注 |
| --- | --- | --- |
| slide（含上下和左右） | 48.9 Hz，22 次 | max_gap 75 ms，slow 10 |
| 　横滑 slide | 每次 50–59 Hz | |
| 　上下滑 | **两种明显不同**：约 60 Hz / 37–45 Hz 交替 | 见下 |
| blinds | 47.1 Hz | |
| iris | 42.2 Hz | 用户说有问题 |
| 列表菜单 | 34.9 Hz，7 次 | max_gap 40 ms，slow 0 |

上下滑的"60 / 40 交替"是**推断**，没有验证：60 Hz 那次是从表盘拉出中心页（只重画已揭开区域），40 Hz 那次是关中心页回表盘（`cover_preview` 路径，`nav_apply` 每帧 `lv_obj_invalidate(g_nav.reveal)` 整屏，CPU 逐行 memcpy 整屏 + 整屏送屏）。下一步先加日志确认，再把关中心页改成"只标变化的那段"。COVER 优化对 40 Hz 那种帮助不大，因为瓶颈在整屏 memcpy 和送屏，不在渲染。

## 5. 待办（按建议顺序）

1. **iris 的问题先问用户现象**：是边缘锯齿/闪、圆内画面错位、残影，还是只是卡？已知线索：
   - iris 走 `nav_flush_start_cb` 逐行拷快照，失效区按 `IRIS_BAND` 分带、左右各一段（`nav_apply`）。rc8 送屏已扩成整行，窄窗口问题不会再出现；但失效区仍是两段窄条，经 rc3 的"本帧 ∪ 上帧"合并后数量可能超过 `LV_INV_BUF_SIZE`(32)，然后退化成整屏重画，这可能就是卡和 42 Hz 的原因；
   - 秒针叠加 `overlay_second_hand` 只在 `src_y == y` 时画，iris 满足这个条件，属于正常路径；
   - `reveal_covers()` 对 iris 返回 false，没改 iris 的渲染。
2. **上下滑关中心页 40 Hz**：先在 `nav_finish` 的 PERF 行加 `cover=%d` 确认推断，再优化 `nav_apply` 的 `cover_preview` 分支。
3. **列表菜单 34.9 Hz**：菜单不走快照路径，瓶颈在 LVGL 渲染（行内文字目前走 CPU，`CONFIG_VELARIDE_EPIC_LABEL` 默认关）。可选：
   - 关调试选项（`CONFIG_DEBUG_FEATURES/ASSERTIONS/INFO`、`-g3`），预计有收益，未测；
   - 列表滚动时整页缓存成快照，平移时用快照拷贝；
   - 修 EPIC 文字"只检查首字形"的问题后，放开文字走 EPIC。
4. **TE 防撕裂**：`CONFIG_LCD_CO5300_VSYNC_ENABLE=y` 一直没生效（`co5300.c` 检查不带 `CONFIG_` 的宏名，`sfconfig.h` 没映射），U7 也一样。`te_sfconfig.patch` 补上了映射；PA02 已配成 `LCDC1_SPI_TE`，但模组有没有接出 TE 不确定，开了可能卡送屏。打补丁：`cd build/migrate/vendor_sifli && git apply ../te_sfconfig.patch`。
5. 整理补丁进 `my-contest-repo`（迁移交接第 6 节的未验证项照旧）。

## 6. 工具变化

- `inc.sh` 支持 `velaride/...` 路径：`inc.sh mig75_rc9 velaride/velaride_shell.c vendor_sifli/...`。
- `setup_mig.sh` 全量重建时会把 `build/migrate/velaride/*.c` 覆盖进 WSL 树（之前不会，全量重建会冲掉应用改动）。
- 新增 `nsh_cmd.py`（向 NSH 发命令并打印输出）、`port_reveal_cover.py`（已执行过，**不要重复执行**）。
- 跑完测试要先结束 `serial_log.py`（`taskkill //F //IM py.exe`），否则 COM5 被占，sftool 返回 101。

## 7. 踩的坑

- Git Bash heredoc 里的 Python 会吃掉 `\n` 的反斜杠，C 字符串被拆成两行，编译报 "unterminated argument list"。含反斜杠的改动用 Write 写 `.py` 文件再执行，或者用 Edit。
- 系统默认编码是 GBK，Python 读 UTF-8 文件必须加 `-X utf8` 或显式 `encoding='utf-8'`。
- `velaride_shell.c` 是 CRLF，改动脚本按 `\r\n` 读写。
- syslog 不进 `dmesg`（没开 RAMLOG），只在串口；`LV_LCD:` 开关日志要在串口录制里看。
