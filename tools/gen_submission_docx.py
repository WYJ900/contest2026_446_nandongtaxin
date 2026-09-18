# -*- coding: utf-8 -*-
"""根据官方模板生成 VelaRide AI 作品提交技术报告 docx。

- 复制官方模板（保留标题、表格、样式）
- 在每个章节提示段落后插入正文
- 填充信息表（Table 2）与 AI-Native 表（Table 3）
- 输出到 D:/desktop/VelaRide_AI/my-contest-repo/submission/

用法: python gen_submission_docx.py
"""
import docx
from docx import Document
from docx.shared import Pt
from docx.enum.text import WD_ALIGN_PARAGRAPH
import copy

TEMPLATE = r"E:\TemporaryDownload\2026 首届 openvela AI 硬件开发者大赛 - 作品提交模板.docx"
OUT = r"D:\desktop\VelaRide_AI\my-contest-repo\submission\VelaRide_AI_技术报告.docx"

doc = Document(TEMPLATE)
paras = doc.paragraphs

# ---------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------
def insert_body_after(idx, text, bold_first=True):
    """在第 idx 段落后插入正文段落（多行）。返回新段落索引（末尾）。"""
    p = paras[idx]
    lines = [ln for ln in text.split("\n") if ln.strip()]
    anchor = p
    new_paras = []
    for ln in lines:
        np = doc.add_paragraph()
        # 复制 anchor 的样式
        if anchor.style:
            try:
                np.style = anchor.style
            except Exception:
                pass
        run = np.add_run(ln)
        run.font.size = Pt(10.5)
        if bold_first and ln.startswith(("1)", "2)", "3)", "4)", "5)", "6)", "7)", "8)", "9)", "10)", "11)", "12)", "13)")):
            pass
        new_paras.append(np)
    return new_paras

def fill_table(tbl, mapping):
    """按 表头-内容 匹配填表。mapping: {第一列文本: 第二列内容}"""
    for row in tbl.rows:
        cells = row.cells
        if len(cells) < 2:
            continue
        key = cells[0].text.strip()
        if key in mapping:
            # 清空第二列已有文本
            for p in cells[1].paragraphs:
                for r in p.runs:
                    r.text = ""
            # 写入
            cells[1].text = mapping[key]

# ---------------------------------------------------------------
# 1. 信息表 (Table 2)
# ---------------------------------------------------------------
info = doc.tables[2]
fill_table(info, {
    "作品名称": "VelaRide AI 骑行安全腕上助手",
    "队伍名称": "南都踏心 (nandongtaxin)",
    "团队分工": "WYJ900：需求拆解、openvela 源码定位、驱动排障、界面实现、性能测量、Web 服务、测试、文档整理与提交",
    "选题方向": "AI 硬件产品创新 + 手表应用创新（组合）",
})

# ---------------------------------------------------------------
# 2. 摘要 (paragraph 6 后, heading_3)
# ---------------------------------------------------------------
abstract = (
    "VelaRide AI 是一款运行在立创黄山派（SF32LB52）上的骑行安全腕上助手，面向城市骑行者解决骑行途中频繁查看手机的安全风险。"
    "设备端基于 openvela 原生 NuttX + LVGL 9.1 实现五表盘二维蜂窝导航、中文运动流程与 LSM6DSL 运动检测，"
    "支持冲击后静止、长时间静止、连续运动三种主动安全提醒；NOR Flash KVDB + CRC32 保证骑行记录跨复位与重烧录一致。"
    "Web 网关调用大赛 Xiaomi MiMo Token Plan（Anthropic-compatible）生成中文骑行复盘，无 Key、超时或失败时明确降级为本地确定性规则。"
    "真机验证 LCD/触摸/RTC/LSM6DSL 与持久化；局部刷新优化使 LCD putarea 调用率提升 12.6 倍，"
    "LVGL C 渲染 228 个采样窗口仅 1 次低于 20fps；7 项 Node 自动测试全过。源码与 AI Coding 日志存于专属仓 contest2026_446_nandongtaxin。"
)
insert_body_after(6, abstract)

# ---------------------------------------------------------------
# 3.1 绪论 (paragraph 9 后, heading_5)
# ---------------------------------------------------------------
s31 = (
    "1) 项目背景与问题定义\n"
    "城市通勤与休闲骑行群体在骑行途中需要频繁查看手机获取速度、距离、导航与运动记录，存在明显安全隐患；"
    "现有骑行记录 App 只被动记录数据，不主动提醒；用户长时间连续骑行易疲劳，缺少休息提示；"
    "骑行结束后缺少简洁、可执行的复盘建议。\n"
    "2) 技术难点\n"
    "① SF32LB52（Cortex-M33）内部 SRAM 仅约 512KB，390×450 RGB565 全屏帧缓冲 351KB 必须落 PSRAM，"
    "QSPI 串行总线导致全屏 memcpy 43.7ms（SRAM 仅 1ms，差约 40 倍），表盘滑动易卡顿；"
    "② LCPU HCI TX 时序问题使 BLE 无线闭环阻塞，需降级为串口/手工录入演示链路；"
    "③ 手表中文字形子集与 AI 自由文本之间的字库覆盖冲突。\n"
    "3) 创新点\n"
    "技术层面：通过局部 60 行刷新把渲染缓冲移入 SRAM，LCD putarea 调用率提升 12.6 倍；"
    "移植 SiFli EPIC GPU draw unit 到 NuttX/LVGL 9.1。"
    "场景层面：腕上主动安全提醒（冲击后静止确认、长时间静止询问、连续运动休息提醒）与设备端确定性规则兜底，"
    "不依赖网络仍可完成核心安全闭环。"
    "交互层面：五表盘二维蜂窝菜单、KEY2 单击/双击盲操作、LVGL C 原生导航替代 JS 驱动，输入到刷新延迟降至 102-146ms。"
)
insert_body_after(9, s31)

# ---------------------------------------------------------------
# 3.2 系统方案设计 (paragraph 13 后, heading_6)
# ---------------------------------------------------------------
s32 = (
    "1) 系统总体架构\n"
    "系统分为设备端、网关端、云端三层：设备端（黄山派 openvela）负责腕上交互、运动检测、本地安全规则与骑行记录；"
    "网关端（web-gateway Node 服务）负责调用云端 AI 生成复盘；云端使用大赛 Xiaomi MiMo Token Plan。"
    "数据流：LSM6DSL 采样 → 确定性规则判断 → 本地记录（NOR Flash）→ 手表结束页 → 手工录入 Web → MiMo 复盘 → 展示。"
    "2) 方案论证与选型\n"
    "开发板选型：立创黄山派 SF32LB52 已官方适配 openvela，板载 AMOLED、触摸、RTC、LSM6DSL、NOR Flash、BLE，适合可穿戴原型，无需额外硬件采购。"
    "端/云职责：设备端保留计时、阈值判断、本地存储与离线提醒等确定性逻辑；云端仅负责自然语言理解与总结，避免纯云端应用。"
    "断网/弱网降级：无 Key、超时（60s）、HTTP 非 2xx 时 Web 网关返回本地确定性中文复盘并标记 source=fallback，"
    "手表本地计时、提醒与记录不受网络影响。"
    "备选对比：快应用（QuickJS 驱动）切卡片掉帧明显（最低 1.5 calls/s），改用 LVGL C 后最低 10.9 calls/s；"
    "完整 SiFli gui_app_fwk 移植工作量过大，自行实现轻量三槽位导航器。\n"
    "3) 关键模块设计\n"
    "通信模块：BLE GATT（FFF0/FFF1/FFF2）JSON 分片协议 time_sync/phone_data/ride_status/ride_review，"
    "网页 18 字节/片、片间 8ms；设备 RX 缓冲 768 字节。"
    "协同模块：ride-safety Skill 规定只读骑行状态与最近记录，暂停/继续须用户明确确认，网络失败不得覆盖本地状态。"
    "交互模块：LVGL C 卡片流 + 固定周期 timer 把一周期内触摸位移合并成一帧，帧间隔均匀。"
)
insert_body_after(13, s32)

# ---------------------------------------------------------------
# 3.3 核心算法与技术原理 (paragraph 17 后, heading_7)
# ---------------------------------------------------------------
s33 = (
    "1) AI 算法实现\n"
    "端侧：设备端不部署神经网络，使用 LSM6DSL 加速度/角速度采样的确定性规则（冲击检测、静止判定、连续运动计时）。"
    "云端：调用大赛 Xiaomi MiMo Token Plan，Anthropic-compatible 接口"
    "（POST https://token-plan-cn.xiaomimimo.com/anthropic/v1/messages），Bearer Token 鉴权，模型 mimo-v2.5/mimo-v2.5-pro，"
    "max_tokens 2048、temperature 0.3、thinking disabled；同时保留 OpenAI-compatible 通道（api.xiaomimimo.com）作备选。"
    "系统提示将输出限制在约 140 个 CJK 字符合法白名单内，生成简短中文复盘。\n"
    "2) 关键机制设计\n"
    "① 冲击后静止：检测到冲击后持续静止，先询问用户是否安全，不自行结束骑行；"
    "② 长时间静止：超出阈值询问暂停或继续；"
    "③ 连续运动：40 分钟休息周期（REST_CYCLE_SEC=40×60）进度条提示休息。"
    "④ 降级融合：无 Key/超时/失败时 fallbackReview 依据 elapsed/moving/still/impacts/pause_count 生成确定性中文复盘。\n"
    "3) openvela 系统能力深度运用\n"
    "图形：LVGL 9.1 原生渲染 + EPIC GPU 加速移植 + 局部刷新；"
    "AI：@system.velaclaw 意图识别、ride-safety 自定义 Skill、工具白名单；"
    "多媒体：手表中文字体按需子集，Web 保留 MiMo 原文。"
    "对 openvela 的优化/拓展：为 NuttX 增加 libuv 事件驱动线程池 overlay、EPIC draw unit 移植、"
    "RTC 年份转换修正与 Flash 时间兜底。改进建议：SRAM 偏小导致大帧缓冲必须落 PSRAM，"
    "建议提供可配置的双缓冲/局部刷新策略文档。"
)
insert_body_after(17, s33)

# ---------------------------------------------------------------
# 3.4 系统实现 (paragraph 21 后, heading_8)
# ---------------------------------------------------------------
s34 = (
    "1) 软件/固件架构\n"
    "app/velaride/：主循环（velaride.c）、运动 UI（velaride_sport_ui.c）、骑行会话持久化（velaride_ride_session.c）、"
    "时间恢复（velaride_timekeeper.c）、BLE 通道（velaride_ble.c）、NSH shell 命令（velaride_shell.c）、中文字体子集。"
    "web-gateway/：server.mjs（health/review API + MiMo 调用 + 降级）、public/（响应式页面 + BLE/录入逻辑）、test/（7 项自动测试）。\n"
    "2) 数据流与关键流程设计\n"
    "骑行流程：进入运动 → 类型/目标/区间/组数选择 → 运动中（计时/速度/距离/心率/暂停继续/退出确认）→ 结束 → 本地复盘。"
    "持久化流程：骑行结束写 KVDB persist.velaride.latest（magic+version+len+CRC32）→ 跨 RTS 复位与重烧录读取一致。\n"
    "3) 硬件设计与适配\n"
    "开发板：立创黄山派 SF32LB52（Cortex-M33）；屏幕 390×450 RGB565 AMOLED；触摸 FT6146；运动传感器 LSM6DSL；"
    "RTC；NOR Flash 4MiB 配置区（0x129a0000）；业务按键 KEY2/PA43（KEY1/PA34 电源与下载不占用）。"
    "是否完成全新硬件平台适配或驱动开发：是。移植 SiFli EPIC GPU 驱动（drv_epic.c）到 NuttX/LVGL 9.1，"
    "新增 LV_USE_DRAW_EPIC Kconfig 与 sf32lb_epic draw unit；适配难点为 PSRAM 不可被 EPIC 访问，需局部刷新落 SRAM。\n"
    "4) 应用/交互端设计\n"
    "五表盘左右滑动 + 蜂窝菜单自由二维拖动；KEY2 单击切表盘/蜂窝、快速双击进运动流程；运动中橙色淡入提醒。"
    "Web 页面两步演示：点\"录入真实手表结果\"（预填手表结束页真实读数 elapsed_s=8 等）→ 点\"生成复盘\"。\n"
    "5) 自定义 Skill\n"
    "自建 ride-safety Skill（agent_skills/ride-safety.md）：运行时位于 /data/agent/skills/。"
    "触发场景：收到骑行状态、静止、冲击、休息提醒或复盘请求时；通过 run_shell 调用 velaride ride-status/ride-latest/ride-pause/ride-resume，"
    "决策边界为冲击后静止先询问安全、无确认不改变状态、网络失败不覆盖本地记录、复盘不虚构数据。"
)
insert_body_after(21, s34)

# ---------------------------------------------------------------
# 3.5 系统测试与结果分析 (paragraph 27 后, heading_9)
# ---------------------------------------------------------------
s35 = (
    "1) 测试环境\n"
    "设备：立创黄山派 SF32LB52，固件 velaride-huangshan-pi-verified.bin（9,655,716 B，SHA-256 E2304000...F1，烧录地址 0x12010000）。"
    "主机：Windows 11 + WSL Ubuntu 22.04；Node.js ≥20。"
    "AI：Xiaomi MiMo Token Plan（Anthropic-compatible，mimo-v2.5-pro）。\n"
    "2) 功能测试\n"
    "真机验证：LCD、触摸、RTC、LSM6DSL、ADC 通过；五表盘与蜂窝菜单通过；中文运动流程通过；骑行记录持久化通过（写入→RTS 复位→重烧录三次一致）。"
    "Web：health 与 review API 冒烟通过；MiMo 真实调用通过（2026-09-18 端到端抓取，页面显示\"由 Xiaomi MiMo 生成\"）。"
    "7 项 Node 自动测试全过（预填一致性 3 + 服务 3 + 启动脚本 1）。\n"
    "3) 性能测试\n"
    "LCD 送屏：局部 60 行缓冲后 putarea 调用率 2.9→36.5 calls/s（12.6 倍）；LVGL C 平均 73.3 calls/s，"
    "228 窗口仅 1 次低于 20fps（最低 10.9 vs 快应用 1.5 calls/s）；flush_avg 3ms。"
    "内存：SRAM 512KB，静态占用约 399-440KB，堆余约 83.6-113KB；PSRAM 用户堆 8.4MB。"
    "延迟：端到端输入→REFR_READY 约 102-146ms，最大约 120-159ms。"
    "Flash 开销：EPIC 单步约 200µs 固定开销，fill/blit 比 CPU 慢 2.7-2.8 倍。"
    "固件大小 9,642,180 B。\n"
    "4) 可靠性与稳定性测试\n"
    "持久化可靠性：测试记录三次读回完全一致（seq=1 end=4 elapsed=321 moving=234 still=87 pause=3 impact=2 rest=1）。"
    "异常恢复：冷启动最多等待 /dev/lcd0、/dev/input0 10 秒，避免节点未建立即退出；启动无断言/HardFault。"
    "Web 降级兜底：无 Key、超时、非 2xx 均返回本地确定性复盘，页面标记降级。"
    "安全/健康类必填项：冲击后静止先询问用户安全；长时间静止提醒给出暂停/继续动作；连续运动 40 分钟休息提示。"
)
insert_body_after(27, s35)

# ---------------------------------------------------------------
# 3.6 AI-Native 开发说明 (paragraph 32 后, heading_10)
# ---------------------------------------------------------------
s36 = (
    "AI 贯穿需求拆解、openvela 源码定位、驱动排障、界面实现、性能测量、Web 服务、测试与文档整理全流程。"
    "Claude Code 辅助完成 LSM6DSL 驱动排障、EPIC GPU 移植代码编写、局部刷新参数调优与固件二分定位（Web/BLE 新增静态缓冲导致的 PSRAM 地址敏感黑屏，"
    "改为按需动态分配后真机亮屏）。性能测量使用板上 VELARIDE_PERF 统计与真机日志，未验证的推断不写入报告。"
    "遇到的主要问题与解决：LVGL framebuffer 落 PSRAM 导致渲染吞吐瓶颈，通过局部 60 行刷新挪入 SRAM 解决；"
    "触摸不跟手经源码复核为未使用 libuv 事件驱动输入，后改用完整 lv_nuttx_uv_init 并保留触摸 fd 驱动刷新节拍。"
)
insert_body_after(32, s36)

# ---------------------------------------------------------------
# AI-Native 表 (Table 3)
# ---------------------------------------------------------------
ai_table = doc.tables[3]
fill_table(ai_table, {
    "AI Coding 代码占比": "约 80%（估算口径：设备端 UI/驱动调试与 Web 网关主体逻辑均由 AI 辅助编写，手工部分为硬件适配细节与阈值调试）",
    "使用的 AI 工具": "Claude Code（对话、编码、调试）、Xiaomi MiMo（AI 复盘生成，Anthropic-compatible）",
    "MCP 工具使用情况": "VelaJS/openvela 相关 MCP（源码定位）、Blender MCP（表盘视觉资源探索，未用于最终交付资源）",
    "Skills 使用与新增情况": "使用：Claude Code 内置工具与自定义 ride-safety Skill；新增：agent_skills/ride-safety.md（运行时 /data/agent/skills/ride-safety.md）",
    "Token 使用总量": "Claude Code 会话约 1147+343+749 事件（见 logs/WYJ900/）；MiMo 用量可在控制台查询",
})

# ---------------------------------------------------------------
# 3.7 总结与展望 (paragraph 34 后, heading_11)
# ---------------------------------------------------------------
s37 = (
    "1) 成果总结\n"
    "完成黄山派骑行安全腕上助手：五表盘二维导航、中文运动全流程、LSM6DSL 三种主动安全提醒、NOR Flash CRC 持久化、"
    "ride-safety Skill、Web 网关 + Xiaomi MiMo 中文复盘、本地降级兜底。7 项自动测试全过，真机验证固件已发布。\n"
    "2) 应用前景与商业价值\n"
    "目标受众：城市通勤与休闲骑行人群（18-45 岁，一二线城市为主，习惯佩戴智能手表、关注运动健康数据）。"
    "商业模式：手表端基础骑行安全功能免费，AI 复盘与高级分析订阅制；可与骑行 App/保险/健康平台合作。"
    "规模化潜力：SF32LB52 已量产芯片，openvela 生态可扩展至更多可穿戴形态。\n"
    "3) 不足与未来工作\n"
    "BLE 无线闭环受 LCPU HCI TX 时序阻塞，需修复后完成真机无线验收；Web 数据录入依赖手工，后续接入 BLE 自动读取；"
    "AI 复盘需更多真机样本验证准确性；功耗测量尚未完成。未来计划：修复 BLE、接入 GPS/心率、实现断线补传、"
    "完成功耗与续航实测、将 EPIC 与局部刷新优化提交 openvela 上游。"
)
insert_body_after(34, s37)

doc.save(OUT)
print("已生成:", OUT)
