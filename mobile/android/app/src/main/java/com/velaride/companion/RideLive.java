package com.velaride.companion;

import java.util.Locale;

/**
 * One watch ride_status reply shaped for the phone dashboard. Pure Java so
 * host tests cover it; the watch polls every 2 s and the clock is
 * extrapolated between replies only while the ride is moving forward.
 */
final class RideLive {
    static final long STALE_MS = 10000;
    final boolean active, paused, safetyPending, phoneValid;
    final int motion, impacts, pauses, rests, safetyRemaining;
    final long elapsedS, movingS, stillS, rideDistanceM, receivedMs;
    final double speedKmh, distanceKm;

    RideLive(boolean active, boolean paused, int motion, long elapsedS, long movingS, long stillS,
             int impacts, int pauses, int rests, boolean phoneValid, double speedKmh, double distanceKm,
             long rideDistanceM, boolean safetyPending, int safetyRemaining, long receivedMs) {
        this.active = active; this.paused = paused; this.motion = motion;
        this.elapsedS = Math.max(0, elapsedS); this.movingS = Math.max(0, movingS); this.stillS = Math.max(0, stillS);
        this.impacts = impacts; this.pauses = pauses; this.rests = rests;
        this.phoneValid = phoneValid && Double.isFinite(speedKmh) && Double.isFinite(distanceKm);
        this.speedKmh = this.phoneValid ? Math.max(0, speedKmh) : 0;
        this.distanceKm = this.phoneValid ? Math.max(0, distanceKm) : 0;
        this.rideDistanceM = Math.max(0, rideDistanceM);
        this.safetyPending = safetyPending; this.safetyRemaining = Math.max(0, safetyRemaining);
        this.receivedMs = receivedMs;
    }

    boolean stale(long nowMs) { return nowMs - receivedMs >= STALE_MS || nowMs < receivedMs; }

    /** Elapsed seconds at nowMs; frozen when paused, stopped or the data is stale. */
    long elapsedAt(long nowMs) {
        if (!active || paused || stale(nowMs)) return elapsedS;
        return elapsedS + (nowMs - receivedMs) / 1000;
    }

    String headline(long nowMs) {
        if (!active) return "手表待机中";
        return (paused ? "已暂停  " : "骑行中  ") + clock(elapsedAt(nowMs));
    }

    String detail(long nowMs) {
        if (!active) return "在手表上开始骑行后，这里实时显示时长、运动与安全状态。";
        StringBuilder text = new StringBuilder();
        text.append("运动 ").append(clock(movingS)).append("    静止 ").append(clock(stillS)).append('\n')
            .append(motion == 2 ? "正在移动" : motion == 1 ? "静止中" : "运动识别中")
            .append(" · 暂停 ").append(pauses).append(" 次 · 撞击 ").append(impacts)
            .append(" 次 · 休息提醒 ").append(rests).append(" 次");
        if (phoneValid) text.append('\n').append(String.format(Locale.ROOT, "%.1f km/h · %.2f km（手机定位）", speedKmh, distanceKm));
        else if (rideDistanceM > 0) text.append('\n').append(String.format(Locale.ROOT, "定位距离 %.2f km", rideDistanceM / 1000.0));
        if (stale(nowMs)) text.append("\n手表数据超过 10 秒未更新，正在等待连接…");
        return text.toString();
    }

    String safety() {
        return active && safetyPending ? "⚠ 手表检测到疑似摔倒，等待腕上确认（" + safetyRemaining + " 秒）" : "";
    }

    static String clock(long seconds) {
        seconds = Math.max(0, seconds);
        return String.format(Locale.ROOT, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
}
