package com.velaride.companion;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.List;
import java.util.Locale;

/** Reproducible local review from recorded facts; never labelled as online AI. */
public final class RideReview {
    public static String local(RideHistory.Entry entry, List<RideHistory.Entry> history) {
        StringBuilder out = new StringBuilder("本地复盘（按记录生成）\n\n");
        out.append("记录 #").append(entry.sequence).append("，时长 ").append(entry.elapsed).append(" 秒；运动 ")
                .append(entry.moving).append(" 秒，静止 ").append(entry.still).append(" 秒。\n");
        if (entry.gps.available()) out.append(String.format(Locale.ROOT,
                "GPS 距离 %.3f km，有效采样均速 %.2f km/h，最高 %.2f km/h；采样 %.1f 秒。\n",
                entry.gps.distanceM / 1000.0, entry.gps.avgCenti / 100.0, entry.gps.maxCenti / 100.0,
                entry.gps.timeMs / 1000.0));
        else out.append("此记录没有有效 GPS 统计，不估算速度和距离。\n");
        out.append("暂停 ").append(entry.pauses).append(" 次，休息提醒 ").append(entry.rests)
                .append(" 次，冲击记录 ").append(entry.impacts).append(" 次。冲击不等于摔车。\n");
        if (entry.safety.available()) out.append(entry.safety.summary()).append('\n');
        else out.append("旧记录没有安全确认的处理结果。\n");
        RideHistory.Entry comparison = null;
        for (RideHistory.Entry other : history) {
            if (other.device.equals(entry.device) && !other.sameRide(entry) && other.firstSync < entry.firstSync
                    && (comparison == null || other.firstSync > comparison.firstSync)) comparison = other;
        }
        if (comparison != null) {
            out.append("\n与同一手表记录 #").append(comparison.sequence).append(" 比较：时长")
                    .append(delta(entry.elapsed - comparison.elapsed, "秒")).append("。");
            if (entry.gps.available() && comparison.gps.available())
                out.append("GPS 距离").append(delta(entry.gps.distanceM - comparison.gps.distanceM, "米")).append("。");
            out.append("对比对象按手机同步顺序选择，不代表骑行发生顺序或健康改善。\n");
        }
        out.append("\n下次建议：").append(entry.safety.help > 0 || entry.safety.unanswered > 0
                ? "出发前确认手机连接与求助方式；异常提醒出现后，在安全地点确认。"
                : entry.elapsed >= 2700 ? "出发前设置可执行的休息间隔，停车后再操作手表。"
                : "保持可执行的骑行目标，停车后处理提醒并同步保存记录。");
        return out.toString();
    }
    private static String delta(long value, String unit) {
        return value == 0 ? "相同" : (value > 0 ? "增加 " : "减少 ") + Math.abs(value) + " " + unit;
    }
    public static String identity(RideHistory.Entry e) {
        String data = e.device.length() + ":" + e.device + ":" + e.sequence + ":" + e.elapsed + ":" + e.moving + ":" + e.still
                + ":" + e.pauses + ":" + e.rests + ":" + e.impacts + ":" + e.gps.timeMs + ":" + e.gps.distanceM
                + ":" + e.gps.avgCenti + ":" + e.gps.maxCenti + ":" + e.safety.version + ":" + e.safety.checks
                + ":" + e.safety.confirmed + ":" + e.safety.pauses + ":" + e.safety.help + ":" + e.safety.unanswered
                + ":" + e.safety.lastAction + ":" + e.safety.lastElapsed;
        try {
            byte[] bytes = MessageDigest.getInstance("SHA-256").digest(data.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder();
            for (byte value : bytes) hex.append(String.format(Locale.ROOT, "%02x", value & 255));
            return hex.toString();
        } catch (Exception error) { throw new IllegalStateException(error); }
    }
    public static String watchText(String source, String text) {
        String prefix = "fallback".equals(source) ? "本地复盘：" : "AI复盘：";
        StringBuilder out = new StringBuilder(prefix);
        for (int i = 0; i < text.length();) {
            int point = text.codePointAt(i);
            String next = new String(Character.toChars(point));
            if ((out.toString() + next).getBytes(StandardCharsets.UTF_8).length > 260) break;
            out.append(next); i += Character.charCount(point);
        }
        return out.toString();
    }
    private RideReview() { }
}
