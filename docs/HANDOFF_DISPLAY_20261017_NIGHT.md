# 显示链路交接（2026-10-17 深夜，rc9 → rc17）

新对话接着做显示/帧率，**先读本页**。更早的背景按时间倒序：

- [晚间交接](HANDOFF_DISPLAY_20261017_EVENING.md)：rc3–rc8，横滑稀碎修复、诊断开关、`reveal_covers()`；
- [迁移交接](HANDOFF_DISPLAY_MIGRATION_20261017.md)：官方显示链路 + EPIC 迁移、工具清单（第 7 节）；
- [EPIC 实测交接](HANDOFF_EPIC_20261017.md)：旧链路 benchmark。

## 1. 一句话现状

用户确认"还可以了"：iris 转场正常，上下滑、横滑、列表菜单都更顺，跟手问题已修。**剩下的主要问题是进出设置子页面会顿一下**（每次整页重建，点一下到出画面约 60–80 ms），已经测清楚，还没开始改。

## 2. 当前状态（不要搞错）

| 项目 | 状态 |
| --- | --- |
| 手表固件 | `build/migrate/out/mig75_rc17.bin`，9,580,896 B，SHA-256 `b04d766146ac6c9b9dda8c7a9e276afdf7e007d5f49b8dba4adfcb7a51c1d81d` |
| 回退镜像 | rc8：`build/migrate/out/mig75_rc8.bin`（`cfbb06a3…02418`）；U7：`my-contest-repo/artifacts/velaride-ui-u7-20261011.bin` |
| 正式源码工作区 | `vendor/sifli`、`apps/graphics/lvgl/lvgl`、`apps/examples/velaride`：**仍未改动** |
| 迁移源码 | `build/migrate/vendor_sifli`、`build/migrate/lvgl`：两个 detached worktree，未提交 |
| 应用源码副本 | `build/migrate/velaride/velaride_shell.c`、`velaride_sport_ui.c`（**本轮新取出**）：普通文件，不是 git，均为 CRLF |
| WSL 迁移树 | `/home/wyj/velaride_mig` = rc17 源码；LVGL 优化级别仍为 `-Os`（`CONFIG_LV_OPTLEVEL=""`） |
| 配置分区 | 未动（镜像结尾 < `0x990000`，`build_mig.sh` 会检查）；备份见迁移交接第 1 节 |

## 3. 本轮改动

### 3.1 iris 残影（rc9）

**根因**：`nav_apply` 把 iris 失效区按 64 行分带，每带的内边界取"离圆心最近那行"的弦长；带内远离圆心的行弦更短，变化的像素没有被标脏，残留旧页面，圆边上出现台阶状缺口。

**修复**：新增 `iris_half_width_min()`，取带内最短的弦作内边界；半径不变时直接返回。

**验证**：`build/migrate/iris_inv_check.py` 按 C 的整数运算逐像素对照。旧算法在 44 组半径变化里共漏标约 17.7 万像素（200→208 一帧就漏 7902），新算法 0 漏标，单帧区域数 ≤ 12（`LV_INV_BUF_SIZE` 为 32）。用户实测确认没问题。

### 3.2 帧循环多睡一个周期（rc14 → rc17，最大收益）

**根因**：`frame_timer_cb` 由 libuv 每 16 ms 触发。libuv 用帧开始前缓存的循环时间重新排定时器、计算 poll 超时，所以超过 16 ms 的帧结束后还要再睡将近 16 ms，帧周期 ≈ 渲染时间 + 16 ms。

- rc14 的做法：回调末尾 `uv_update_time()`。列表 28.8 → 44 Hz，但**引入了"不跟手"**：已到期的定时器会在同一次 `uv__run_timers()` 里立刻再跑一帧，循环不进 poll，触摸 fd 读不到。
- **rc17 的做法（现行）**：刷新时间后 `uv_timer_start(timer, cb, max(1, 16 - 本帧耗时), 16)`，保证每两帧之间至少 poll 一次（`FRAME_PERIOD_MS`，见 `port_uv_time2.py`）。用户确认跟手。

手指拖动原本就比自动滚动快，因为触摸事件会提前唤醒 poll。所有超过 16 ms 的帧都受益。

### 3.3 诊断工具（都留在 rc17 里）

| 工具 | 用法 / 输出 |
| --- | --- |
| `VELARIDE_PERF` 新增 `cover=0/1` | 区分上下滑是"拉出中心页"还是"关中心页" |
| `velaride list-bench [n]` | 打开列表菜单，自动上下滚 n 次，打 `VELARIDE_MENU_PERF` |
| `velaride ui-bench` | 打开设置页 → 列表上下滚 8 次 → 逐个进出 7 个子页，打 `VELARIDE_UI_BENCH: page=N build=…us`（N 是 `enum velaride_info_e`：1 连接、2 设置、3 帮助、4 屏幕、5 指南针、6 设备状态、7 外观、11 亮屏与熄屏） |
| `/data/lv_prof` | DWT 周期计数，每 32 帧打一行 `LV_PROF:`；超过 30 ms 的帧另打 `LV_SLOW:` |

两个 bench 会自己唤醒屏幕，运行期间保持亮屏。

`LV_PROF` 字段：`pre` = REFR_START→RENDER_START（layout、失效）；`render` = 到最后一块交给 drv_lcd_fb；`post` = 到 REFR_READY；`gap` = 两帧之间；`blk` = 渲染中阻塞在 `lcd_flush` 的时间（其中 `copywait` 等上一块拷贝，`linewait` 等 LCD 送完这些行）；`waitcb` = LVGL 等绘制缓冲；`send` = LCD 送一次屏的时长；`rows` 每次送屏的行数；`chunks` / `kpx` 每帧的块数和像素数。

### 3.4 改动文件与脚本

| 文件 | 改动 | 脚本（都已执行，**不要重复执行**） |
| --- | --- | --- |
| `velaride/velaride_shell.c` | iris 内边界、PERF `cover=`、list-bench、ui-bench、bench 唤醒、帧调度 | `port_iris_fix.py`、`port_list_bench.py`、`port_ui_bench.py`、`port_bench_wake.py`、`port_uv_time.py`、`port_uv_time2.py` |
| `velaride/velaride_sport_ui.c` | 新增 `velaride_sport_ui_bench_scroller()`（只给 ui-bench 用） | `port_ui_bench.py` |
| `vendor_sifli/.../lcd/lv_lcd.c` | `/data/lv_prof`、`LV_SLOW` | `port_lv_prof.py`、`port_lv_prof2.py`、`port_ui_bench.py` |
| `vendor_sifli/.../lcd/drv_lcd_fb.c` | 送屏、拷贝等待、行等待计时变量 | `port_lv_prof.py` |

各轮改动前的源文件备份在 `build/migrate/out/*.bak`（如 `velaride_shell.rc8.c.bak`）。另外新增 `lv_optlevel.sh <名> <"-O2"|"">`：只改 LVGL 的优化级别，重新配置后编译。

## 4. 实测数据

| 场景 | rc8 | rc9 | rc17 | 说明 |
| --- | --- | --- | --- | --- |
| 列表菜单（自动滚动） | — | 28.8 Hz | 41–42 Hz | rc14 是 44 Hz，代价是每帧 1 ms poll |
| 列表菜单（用户手滑） | 34.9 Hz | 30–36 Hz | 平均 42.1 Hz（12 次） | |
| 横滑 slide | 50–59 Hz | 平均 56.6 Hz | rc14 约 55–60 Hz | |
| 上下滑 cover=0 / cover=1 | — | 44.5 / 48.7 Hz | rc14 约 50 / 58 Hz | |
| iris | 42.2 Hz | 43.3 Hz | — | 残影已修 |
| 设置列表滚动 | — | — | 37–40 Hz | 每帧渲染约 24 ms，131 kpx |

rc14 的数据来自 `out/rc14_test.log`；rc17 只录到列表手滑（`out/rc17_test.log`），其他场景帧调度与 rc14 相同，只多了 1 ms poll。

**列表菜单每帧拆分**（rc14，整屏 175 kpx，6 块）：渲染约 21.5 ms；LCD 送整屏约 21 ms，与渲染并行（两者抢 PSRAM，单独送屏约 15.5 ms）。整屏滚动的上限大约 47 Hz，要再提升必须同时降低渲染耗时和 PSRAM 争用。

**进出设置子页面**（rc16/17 `ui-bench`）：

| 页面 | 建页耗时 |
| --- | --- |
| 设置（每次返回都重建） | 33 ms |
| 外观 | 38–39 ms |
| 亮屏与熄屏 | 30 ms |
| 屏幕设置 | 20 ms |
| 指南针 | 16 ms |
| 设备状态 / 连接 / 帮助 | 11–13 ms |

建页之后的首帧还有 `pre`（layout）约 15 ms + `render` 25–30 ms。所以从点一下到出画面约 60–80 ms，这就是用户说的"子目录卡"。指南针页每帧渲染约 30 ms，一直在刷新。

**已排除**：
- 只给 LVGL 换 `-O2`：28.8 → 28.9 Hz，无效，已恢复 `-Os`；
- 晚间交接里"关中心页 40 Hz、拉出 60 Hz"的推断不成立，两个方向都在 45–49 Hz（rc9）。

## 5. 待办（按建议顺序）

1. **设置页常驻**：`velaride_sport_ui.c` 的 `clear_page()` → `clear_page_state()` 每次都 `lv_obj_clean(g_sport.root)`。改成设置列表放在单独的容器里，进子页时隐藏、返回时显示，省掉返回时的 33 ms 重建以及那一帧的排版和绘制。注意 `g_sport.body` 等指针在 `clear_page_state()` 里清零，子页也在用。
2. **查建页为什么慢**（10–39 ms，外观页最慢）：可以在 `velaride_sport_ui_show_info()` 里分段计时。怀疑是逐条 `lv_obj_set_style_*` 触发的样式刷新和中文 label 排版；可以考虑共享 `lv_style_t`。
3. **首帧 layout 15 ms**：大量 `lv_obj_align` / 绝对定位对象的首次布局，可能和第 2 项同源。
4. 如果还是顿：给进出子页加快照转场，把建页时间藏在动画后面。
5. 列表/整屏滚动想超过约 47 Hz：放开文字走 EPIC（`CONFIG_VELARIDE_EPIC_LABEL`，要先修"只检查首字形"的问题），或者列表滚动用快照平移。
6. 晚间交接遗留：TE 防撕裂补丁 `te_sfconfig.patch` 未测；关调试选项（`DEBUG_FEATURES/ASSERTIONS/INFO`、`-g3`）未测；补丁整理进 `my-contest-repo`。

## 6. 操作方法

```sh
# 增量编译（从 Windows 工作区根目录）
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/build/migrate/inc.sh mig75_rc18 velaride/velaride_shell.c velaride/velaride_sport_ui.c
# 烧录
./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "build/migrate/out/mig75_rc18.bin@0x12010000"
# 开机约 20 秒后跑自动测试
py -3.13 -X utf8 build/migrate/nsh_cmd.py --wait 25 "" "echo 1 > /data/lv_prof" "velaride ui-bench"
py -3.13 -X utf8 build/migrate/nsh_cmd.py --wait 14 "" "velaride list-bench 8"
# 请用户实测时录串口（录完要结束，否则 COM5 被占）
py -3.13 -X utf8 build/migrate/serial_log.py --port COM5 --minutes 8 --log build/migrate/out/<名>.log
taskkill //F //IM py.exe
```

## 7. 踩的坑

- **串口控制台可能被 ai_agent 占用**：提示符变成 `vela>`，不认 `velaride` 命令。发 `quit` 回到 NSH（会退出 agent，`reboot` 后恢复）。
- **屏幕熄灭时不绘制**：熄屏后 LVGL 照常跑但跳过绘制，bench 和 lv_prof 都没输出。现在两个 bench 会自己唤醒；手动测试前可以先 `velaride lcd-wake`。
- `nsh_cmd.py` 和 `serial_log.py` 不能同时开 COM5。要同时录日志，用 `nsh_cmd.py --wait N … > log`。
- **换行格式**：`velaride/*.c` 是 CRLF，`vendor_sifli` 下的驱动是 LF。Python 读写一律用 `newline=''`，否则 Windows 会把 LF 改成 CRLF（rc12/13 的 `lv_lcd.c`、`drv_lcd_fb.c` 中过招，rc15 已恢复）。Git Bash 的 `sed -i` 会把 CRLF 改成 LF，不要用它改 `velaride_shell.c`。
- **Git Bash heredoc 会吃掉反斜杠**：C 字符串里的 `\n` 会被拆成两行。含反斜杠的改动用 Write 写 `.py` 文件再执行。
- 改 `.config` 后，`build_mig.sh` 不会自己重新配置，要先 `cmake -B $OUT -S nuttx`（`lv_optlevel.sh` 已包含这一步）。
- 后台录串口的任务被 `taskkill` 结束后会显示"失败（exit 1）"，属于正常。
