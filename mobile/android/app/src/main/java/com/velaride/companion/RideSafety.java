package com.velaride.companion;

/** Safety confirmation results, not crash diagnoses or delivery receipts. */
public final class RideSafety {
    public static final RideSafety NONE = new RideSafety(0, 0, 0, 0, 0, 0, 0, 0);
    public final long version, checks, confirmed, pauses, help, unanswered, lastAction, lastElapsed;

    public RideSafety(long version, long checks, long confirmed, long pauses, long help,
                      long unanswered, long lastAction, long lastElapsed) {
        if (version < 0 || version > 1 || lastAction < 0 || lastAction > 4
                || lastElapsed < 0 || lastElapsed > 0xffffffffL)
            throw new IllegalArgumentException("安全确认字段异常");
        for (long value : new long[]{checks, confirmed, pauses, help, unanswered})
            if (value < 0 || value > 65535) throw new IllegalArgumentException("安全确认计数异常");
        if (confirmed + pauses + help > checks || unanswered > checks
                || (version == 0 && (checks != 0 || confirmed != 0 || pauses != 0 || help != 0
                    || unanswered != 0 || lastAction != 0 || lastElapsed != 0))
                || (checks == 0 && (lastAction != 0 || lastElapsed != 0)))
            throw new IllegalArgumentException("安全确认统计不一致");
        this.version = version; this.checks = checks; this.confirmed = confirmed; this.pauses = pauses;
        this.help = help; this.unanswered = unanswered; this.lastAction = lastAction; this.lastElapsed = lastElapsed;
    }
    public boolean available() { return version == 1; }
    public String summary() {
        if (!available()) return "安全确认  —（旧记录未提供处理结果）";
        return "安全确认  " + checks + " 次 · 我没事  " + confirmed + " 次\n暂停休息  " + pauses
                + " 次 · 求助意向  " + help + " 次\n未及时确认  " + unanswered
                + " 次（包含之后才确认的情况）\n求助意向只记录选择，不表示已发送消息。";
    }
}
