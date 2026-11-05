package com.velaride.companion;

import java.util.*;

/** Emergency contacts and the SMS text. Pure Java so it can be tested off-device. */
public final class EmergencyPlan {
    public static final int MAX_CONTACTS = 3;
    public static final int UNANSWERED_COUNTDOWN_S = 15, HELP_COUNTDOWN_S = 5;

    public static final class Contact {
        public final String name, number;
        Contact(String name, String number) { this.name = name; this.number = number; }
    }

    /** One contact per line: "姓名 号码" or just a number. Throws with a readable reason. */
    public static List<Contact> parse(String text) {
        List<Contact> contacts = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (String raw : text.split("\\r?\\n")) {
            String line = raw.trim();
            if (line.isEmpty()) continue;
            int split = Math.max(line.lastIndexOf(' '), Math.max(line.lastIndexOf('\t'), line.lastIndexOf('，')));
            String name = split > 0 ? line.substring(0, split).trim() : "";
            String number = normalize(split > 0 ? line.substring(split + 1) : line);
            if (number == null) throw new IllegalArgumentException("号码格式不正确：" + line);
            if (!seen.add(number)) continue;
            if (contacts.size() == MAX_CONTACTS) throw new IllegalArgumentException("最多设置 " + MAX_CONTACTS + " 位紧急联系人");
            if (name.length() > 16) name = name.substring(0, 16);
            contacts.add(new Contact(name.isEmpty() ? "联系人" + (contacts.size() + 1) : name, number));
        }
        return contacts;
    }

    static String normalize(String value) {
        String number = value.trim().replace("-", "").replace(" ", "");
        if (number.startsWith("+86")) number = number.substring(3);
        if (!number.matches("\\+?[0-9]{3,20}")) return null;
        return number;
    }

    public static String format(List<Contact> contacts) {
        StringBuilder out = new StringBuilder();
        for (Contact c : contacts) out.append(c.name).append(' ').append(c.number).append('\n');
        return out.toString().trim();
    }

    public static int countdown(FallAlert.Reason reason) {
        return reason == FallAlert.Reason.HELP ? HELP_COUNTDOWN_S : UNANSWERED_COUNTDOWN_S;
    }

    /**
     * @param fixAgeS seconds since the location fix, negative when there is no fix.
     * Coordinates are WGS84 from Android; the AMap link is told so explicitly.
     */
    public static String message(FallAlert.Reason reason, String clock, double lat, double lon,
                                 float accuracyM, long fixAgeS) {
        StringBuilder text = new StringBuilder("【VelaRide 骑行求助】");
        if (reason == FallAlert.Reason.HELP) text.append("骑行者在手表上选择了“需要帮助”。");
        else if (reason == FallAlert.Reason.DRILL) text.append("这是一条演练测试短信，无需处理。");
        else text.append("手表检测到疑似摔倒/撞击，30 秒内未确认安全。");
        text.append("时间 ").append(clock).append("。");
        if (fixAgeS >= 0 && Math.abs(lat) <= 90 && Math.abs(lon) <= 180 && !(lat == 0 && lon == 0)) {
            text.append("位置 https://uri.amap.com/marker?position=")
                .append(String.format(Locale.ROOT, "%.6f,%.6f", lon, lat))
                .append("&coordinate=wgs84&name=VelaRide");
            text.append("（精度约 ").append(Math.max(1, Math.round(accuracyM))).append(" 米");
            if (fixAgeS >= 120) text.append("，").append(fixAgeS / 60).append(" 分钟前");
            text.append("）。");
        } else text.append("手机暂未取得定位。");
        if (reason != FallAlert.Reason.DRILL) text.append("请尽快电话联系。");
        return text.toString();
    }
}
