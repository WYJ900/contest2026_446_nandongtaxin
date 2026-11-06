package com.velaride.companion;

import android.os.Build;
import java.util.Locale;

/**
 * ROM-specific labels and package names. Only Xiaomi exposes a weather
 * provider to third-party apps; OPPO/realme/OnePlus lock their battery and
 * auto-start pages behind oplus.permission.OPLUS_COMPONENT_SAFE.
 */
final class PhoneVendor {
    static final int XIAOMI = 1, OPLUS = 2, OTHER = 0;
    final int family;
    final String name;

    private PhoneVendor(int family, String name) { this.family = family; this.name = name; }

    static PhoneVendor current() {
        String brand = (Build.BRAND + " " + Build.MANUFACTURER).toLowerCase(Locale.ROOT);
        if (brand.contains("xiaomi") || brand.contains("redmi") || brand.contains("poco"))
            return new PhoneVendor(XIAOMI, brand.contains("redmi") ? "红米" : "小米");
        if (brand.contains("realme")) return new PhoneVendor(OPLUS, "真我");
        if (brand.contains("oneplus")) return new PhoneVendor(OPLUS, "一加");
        if (brand.contains("oppo")) return new PhoneVendor(OPLUS, "OPPO");
        return new PhoneVendor(OTHER, "手机");
    }

    boolean systemWeatherReadable() { return family == XIAOMI; }
    String weatherPackage() { return family == XIAOMI ? "com.miui.weather2" : family == OPLUS ? "com.coloros.weather2" : null; }
    String[] healthPackages() {
        return family == XIAOMI ? new String[]{"com.mi.health", "com.xiaomi.wearable"}
                : new String[]{"com.heytap.health", "com.huawei.health", "com.mi.health", "com.xiaomi.wearable"};
    }
    String backgroundHint() {
        if (family == OPLUS) return "应用详情 → 耗电管理：打开“允许完全后台行为”“允许自启动”“允许关联启动”；最近任务里下拉卡片加锁。";
        if (family == XIAOMI) return "应用详情 → 省电策略选“无限制”，并打开“自启动”。";
        return "在应用详情中关闭电池优化并允许后台运行。";
    }
    String smsHint() {
        return family == OPLUS ? "真我/OPPO 请在应用权限 → 短信中选“始终允许”，否则系统会在发送前弹窗确认。"
                : family == XIAOMI ? "红米手机请在应用权限中把“发送短信”设为始终允许。" : "请确认短信权限为始终允许。";
    }
}
