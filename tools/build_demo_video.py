# VelaRide AI 演示视频构建脚本
# 输入：A段（手表竖屏实拍）+ B段（网页录屏）+ TTS旁白
# 输出：1920x1080 30fps H.264 MP4，带中文旁白
#
# 布局：A段竖屏放右侧（540x960），左侧深色背景+标题/字幕；B段居中放大。
#
#   python tools/build_demo_video.py
#
# 依赖：imageio-ffmpeg（pip install imageio-ffmpeg）、PIL、System.Speech（Windows TTS）

import os
import sys
import subprocess
import tempfile
from pathlib import Path

import imageio_ffmpeg
from PIL import Image, ImageDraw, ImageFont

FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
DESKTOP = Path("D:/desktop")
OUT_DIR = Path("D:/desktop/VelaRide_AI/my-contest-repo/submission-preview")
OUT_DIR.mkdir(parents=True, exist_ok=True)
TMP = Path(tempfile.mkdtemp(prefix="velaride_video_"))

A_VIDEO = DESKTOP / "3bdfd1f98c7b73654e8fc31346b01b55.mp4"
B_VIDEO = DESKTOP / "PixPin_2026-09-18_12-45-02.mp4"
FINAL = OUT_DIR / "VelaRide_AI_demo.mp4"

W, H = 1920, 1080
FPS = 30
BG = (11, 20, 16)          # 深绿黑
ACCENT = (212, 255, 63)     # 柠檬绿
WHITE = (235, 240, 232)
DIM = (140, 160, 140)
FONT_BOLD = "C:/Windows/Fonts/msyhbd.ttc"
FONT_REG = "C:/Windows/Fonts/msyh.ttc"
FONT_LIGHT = "C:/Windows/Fonts/msyhl.ttc"

def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd)[:160])
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout[-500:]); print(r.stderr[-1500:])
        raise RuntimeError(f"命令失败 exit={r.returncode}")
    return r

TTS_DIR = OUT_DIR / "_tts"

# ---------- TTS 旁白（由 gen_tts.ps1 预生成） ----------
def wav_duration(wav):
    import wave
    with wave.open(str(wav), "rb") as f:
        return f.getnframes() / f.getframerate()

def load_tts():
    for seg in SEGMENTS:
        wav = TTS_DIR / f"tts_{seg['name']}.wav"
        if not wav.exists():
            raise FileNotFoundError(f"缺少 TTS 文件 {wav}，请先运行 gen_tts.ps1")
        seg["tts"] = str(wav)
        seg["tts_dur"] = wav_duration(wav)
        print(f"  {seg['name']}: {seg['tts_dur']:.1f}s")

# ---------- 静态背景帧生成 ----------
def make_bg(left_title, left_lines, duration, watch_img=None, a_clip=None):
    """生成一段时长 duration 秒的背景段：左侧标题+字幕，右侧（由 ffmpeg 叠加视频）。
    返回单张 PNG 作为背景，视频叠加由 ffmpeg 完成。这里只做静态层。"""
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    # 顶部品牌条
    f_eyebrow = ImageFont.truetype(FONT_BOLD, 24)
    f_title = ImageFont.truetype(FONT_BOLD, 52)
    f_h2 = ImageFont.truetype(FONT_BOLD, 38)
    f_body = ImageFont.truetype(FONT_REG, 30)
    f_small = ImageFont.truetype(FONT_LIGHT, 22)
    d.text((80, 56), "VELARIDE AI", font=f_eyebrow, fill=ACCENT)
    d.text((80, 96), left_title, font=f_title, fill=WHITE)
    y = 200
    for line in left_lines:
        if isinstance(line, tuple):
            text, color = line
        else:
            text, color = line, DIM
        d.text((80, y), text, font=f_body, fill=color)
        y += 50
    # 左下角水印
    d.text((80, H - 60), "openvela 黄山派 · SF32LB52", font=f_small, fill=DIM)
    # 右侧手表区域占位框（实际视频由 ffmpeg overlay）
    d.rectangle([1280, 60, 1860, 1020], outline=(40, 60, 45), width=2)
    return img

def save_bg(img, path):
    img.save(path, quality=92)

# ---------- 段落定义 ----------
# 每段：name, a_start, a_end（A段裁剪）, title, lines, narration
SEGMENTS = [
    {
        "name": "01_dial",
        "a_start": 0.0, "a_end": 9.0,
        "title": "腕上交互",
        "lines": [("五个表盘 + 二维蜂窝菜单", WHITE),
                  ("LVGL 原生 · 离线资源缓存", DIM),
                  ("KEY2 单击进入菜单", DIM)],
        "narration": "VelaRide AI 运行在 openvela 黄山派上。手表提供五个表盘和可二维拖动的蜂窝菜单，全部用 LVGL 原生实现。",
    },
    {
        "name": "02_config",
        "a_start": 19.0, "a_end": 26.0,
        "title": "运动配置",
        "lines": [("双击 KEY2 进入运动流程", WHITE),
                  ("训练目标 / 心率区间", DIM),
                  ("3-2-1 开始骑行", DIM)],
        "narration": "快速双击实体按键进入运动流程。选择运动类型和目标后，直接在手表上开始骑行。",
    },
    {
        "name": "03_ride",
        "a_start": 26.0, "a_end": 36.0,
        "title": "骑行与本地复盘",
        "lines": [("LSM6DSL 实时运动检测", WHITE),
                  ("运动 / 静止 / 冲击 / 暂停", DIM),
                  ("NOR Flash + KVDB + CRC", DIM),
                  ("跨复位记录不丢失", ACCENT)],
        "narration": "传感器持续采集运动数据，本地规则判断运动、静止和冲击。骑行记录存入独立 Flash 区，跨复位不丢失。",
    },
    {
        "name": "04_mimo",
        "b_video": True,
        "title": "Xiaomi MiMo 复盘",
        "lines": [("网页录入真实手表数据", WHITE),
                  ("调用 Xiaomi MiMo", ACCENT),
                  ("自然语言骑行复盘", WHITE),
                  ("失败时明确标记降级", DIM)],
        "narration": "网页录入刚才手表生成的真实数据，调用小米 MiMo 生成自然语言骑行复盘。页面会明确标记来源，不冒充 AI 结果。",
    },
    {
        "name": "05_outro",
        "a_start": 33.0, "a_end": 36.0,
        "title": "骑行安全 · 腕上完成",
        "lines": [("腕上交互 · 运动检测 · 本地规则", WHITE),
                  ("持久化记录 · 真实 MiMo 复盘", WHITE),
                  ("", DIM),
                  ("github.com/open-vela/contest2026_446_nandongtaxin", ACCENT)],
        "narration": "VelaRide AI 已完成腕上交互、运动检测、本地安全规则和真实 MiMo 复盘。",
    },
]

def build():
    print("=== 1. 加载预生成 TTS 旁白 ===")
    load_tts()

    print("=== 2. 裁剪 A 段视频子片段 ===")
    a_clips = {}
    for seg in SEGMENTS:
        if "a_start" in seg:
            dur = seg["a_end"] - seg["a_start"]
            out = TMP / f"a_{seg['name']}.mp4"
            run([FFMPEG, "-y", "-loglevel", "error",
                 "-ss", str(seg["a_start"]), "-t", str(dur),
                 "-i", str(A_VIDEO),
                 "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                 "-r", str(FPS),
                 str(out)])
            a_clips[seg["name"]] = out
            seg["a_clip"] = str(out)

    print("=== 3. 生成背景帧 ===")
    for seg in SEGMENTS:
        bg = make_bg(seg["title"], seg["lines"], 1)
        bg_path = TMP / f"bg_{seg['name']}.png"
        save_bg(bg, bg_path)
        seg["bg"] = str(bg_path)

    print("=== 4. 合成每段为 1080p 视频 ===")
    seg_videos = []
    for i, seg in enumerate(SEGMENTS):
        out = TMP / f"seg_{i:02d}_{seg['name']}.mp4"
        target_dur = seg["tts_dur"] + 1.5  # 旁白时长 + 1.5s 尾部留白

        if seg.get("b_video"):
            # B段：网页录屏居中放大，背景叠加
            # 先把 B 段缩放到 1280x宽 保持比例，居中放在深色背景上
            cmd = [FFMPEG, "-y", "-loglevel", "error",
                   "-loop", "1", "-t", str(target_dur), "-i", seg["bg"],
                   "-i", str(B_VIDEO),
                   "-filter_complex",
                   f"[1:v]scale=1280:-1,setsar=1,fps={FPS},"
                   f"pad=ceil(iw/2)*2:ceil(ih/2)*2:color=0x0b1410,"
                   f"trim=duration={target_dur},setpts=PTS-STARTPTS[v1];"
                   f"[0:v][v1]overlay=(W-w)/2:(H-h)/2:shortest=1[v]",
                   "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                   "-r", str(FPS), str(out)]
        else:
            # A段：竖屏视频放右侧，背景在左
            a = seg["a_clip"]
            cmd = [FFMPEG, "-y", "-loglevel", "error",
                   "-loop", "1", "-t", str(target_dur), "-i", seg["bg"],
                   "-i", a,
                   "-filter_complex",
                   f"[1:v]scale=540:960,setsar=1,fps={FPS},"
                   f"trim=duration={target_dur},setpts=PTS-STARTPTS,"
                   f"loop=loop={int(target_dur*FPS)}:size={int((seg['a_end']-seg['a_start'])*FPS)}[v1];"
                   f"[0:v][v1]overlay=1300:60:shortest=1[v]",
                   "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                   "-r", str(FPS), str(out)]
        run(cmd)
        seg_videos.append(str(out))
        print(f"  seg {i} done -> {out.name}")

    print("=== 5. 合并 TTS 音轨到各段 ===")
    seg_with_audio = []
    for i, seg in enumerate(SEGMENTS):
        v = seg_videos[i]
        a = seg["tts"]
        out = TMP / f"final_{i:02d}.mp4"
        run([FFMPEG, "-y", "-loglevel", "error",
             "-i", v, "-i", a,
             "-c:v", "copy", "-c:a", "aac", "-b:a", "128k",
             "-shortest", str(out)])
        seg_with_audio.append(str(out))

    print("=== 6. 拼接所有段 ===")
    concat_txt = TMP / "concat.txt"
    concat_txt.write_text(
        "\n".join(f"file '{p}'" for p in seg_with_audio) + "\n",
        encoding="utf-8")
    merged = TMP / "merged.mp4"
    run([FFMPEG, "-y", "-loglevel", "error",
         "-f", "concat", "-safe", "0", "-i", str(concat_txt),
         "-c", "copy", str(merged)])

    print("=== 7. 输出最终成片 ===")
    run([FFMPEG, "-y", "-loglevel", "error",
         "-i", str(merged),
         "-c:v", "libx264", "-preset", "medium", "-crf", "20",
         "-pix_fmt", "yuv420p", "-r", str(FPS),
         "-c:a", "aac", "-b:a", "128k",
         "-movflags", "+faststart",
         str(FINAL)])

    # 统计
    import cv2
    cap = cv2.VideoCapture(str(FINAL))
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    cap.release()
    size = os.path.getsize(FINAL)
    print(f"\n=== 完成 ===")
    print(f"文件: {FINAL}")
    print(f"时长: {n/fps:.1f}s   分辨率: {W}x{H}   {fps:.0f}fps")
    print(f"大小: {size/1024/1024:.1f} MB")

if __name__ == "__main__":
    build()
