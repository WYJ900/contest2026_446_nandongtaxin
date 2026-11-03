package com.velaride.companion;

/** Recorded reminder choices and lap/goal summaries; unknown is distinct from zero. */
public final class RideExtras {
    public static final RideExtras NONE = new RideExtras(0,0,0,0,0,0,0,0,0,0);
    public final long version, goalM, laps, restTaken, restSnoozed, restIgnored, drinks, drinkDone, drinkSnoozed, drinkIgnored;
    public RideExtras(long version, long goalM, long laps, long restTaken, long restSnoozed, long restIgnored,
                      long drinks, long drinkDone, long drinkSnoozed, long drinkIgnored) {
        if (version < 0 || version > 1 || goalM < 0 || goalM > 2000000)
            throw new IllegalArgumentException("骑行目标字段异常");
        for (long value : new long[]{laps,restTaken,restSnoozed,restIgnored,drinks,drinkDone,drinkSnoozed,drinkIgnored})
            if (value < 0 || value > 65535) throw new IllegalArgumentException("骑行提醒计数异常");
        if (drinkDone + drinkSnoozed + drinkIgnored > drinks || (version == 0 &&
                goalM+laps+restTaken+restSnoozed+restIgnored+drinks+drinkDone+drinkSnoozed+drinkIgnored != 0))
            throw new IllegalArgumentException("骑行提醒统计不一致");
        this.version=version; this.goalM=goalM; this.laps=laps; this.restTaken=restTaken;
        this.restSnoozed=restSnoozed; this.restIgnored=restIgnored; this.drinks=drinks;
        this.drinkDone=drinkDone; this.drinkSnoozed=drinkSnoozed; this.drinkIgnored=drinkIgnored;
    }
    public boolean available() { return version == 1; }
    public String summary() {
        if (!available()) return "目标/提醒处理  —（旧记录未提供）";
        return "分段  " + laps + " 次\n休息选择  " + restTaken + " 次 · 稍后  " + restSnoozed + " 次 · 忽略  "
                + restIgnored + " 次\n补水提醒  " + drinks + " 次 · 已补水  " + drinkDone + " 次 · 稍后  "
                + drinkSnoozed + " 次 · 忽略  " + drinkIgnored + " 次\n距离目标  "
                + (goalM == 0 ? "未设置" : goalM + " 米");
    }
}
