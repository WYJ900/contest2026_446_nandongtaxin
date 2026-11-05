package com.velaride.companion;

import java.util.List;

public final class FallAlertTest {
    private static void expect(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    private static int kind(FallAlert a, boolean active, boolean pending, int remaining, long help, int last) {
        return a.update(true, active, pending, remaining, help, last).kind;
    }

    public static void main(String[] args) {
        FallAlert a = new FallAlert();
        expect(a.update(false, true, true, 0, 0, 0).kind == FallAlert.UNSUPPORTED, "old firmware reported");

        // Watch countdown runs, expires unanswered: arm exactly once.
        expect(kind(a, true, false, 0, 0, 0) == FallAlert.NONE, "idle ride");
        FallAlert.Decision warn = a.update(true, true, true, 30, 0, 0);
        expect(warn.kind == FallAlert.WARN && warn.remaining == 30, "warning while watch waits");
        FallAlert.Decision arm = a.update(true, true, true, 0, 0, 0);
        expect(arm.kind == FallAlert.ARM && arm.reason == FallAlert.Reason.UNANSWERED, "unanswered arms");
        expect(kind(a, true, true, 0, 0, 4) == FallAlert.NONE, "no second arm in same episode");
        // Rider answers late on the watch: phone countdown is cancelled.
        expect(kind(a, true, false, 0, 0, FallAlert.ACTION_OK) == FallAlert.CANCEL, "late OK cancels");
        expect(!a.isArmed(), "disarmed after cancel");

        // Late "pause" also proves the rider responded.
        kind(a, true, true, 5, 0, 0); kind(a, true, true, 0, 0, 4);
        expect(kind(a, true, false, 0, 0, FallAlert.ACTION_PAUSE) == FallAlert.CANCEL, "late pause cancels");

        // Explicit help on the watch: arm with HELP reason, once per press.
        expect(kind(a, true, true, 20, 0, 0) == FallAlert.WARN, "new episode warns");
        FallAlert.Decision help = a.update(true, true, false, 0, 1, FallAlert.ACTION_HELP);
        expect(help.kind == FallAlert.ARM && help.reason == FallAlert.Reason.HELP, "help arms");
        expect(kind(a, true, false, 0, 1, FallAlert.ACTION_HELP) == FallAlert.NONE, "same help not repeated");
        expect(kind(a, true, false, 0, 1, FallAlert.ACTION_HELP) == FallAlert.NONE, "help is not auto-cancelled");

        // A help recorded before this phone connected must not call anyone.
        FallAlert fresh = new FallAlert();
        expect(kind(fresh, true, false, 0, 2, FallAlert.ACTION_HELP) == FallAlert.NONE, "stale help ignored");
        expect(kind(fresh, true, false, 0, 3, FallAlert.ACTION_HELP) == FallAlert.ARM, "new help after baseline");

        // Reconnect keeps the baseline: an already handled help is not repeated...
        fresh.reset();
        expect(kind(fresh, true, false, 0, 3, FallAlert.ACTION_HELP) == FallAlert.NONE, "handled help not repeated");
        // ...but help pressed while the link was down escalates after reconnect.
        fresh.reset();
        expect(kind(fresh, true, false, 0, 4, FallAlert.ACTION_HELP) == FallAlert.ARM, "help during disconnect");
        fresh.disarm();
        // A watch countdown that expired while disconnected still arms.
        fresh.reset();
        expect(kind(fresh, true, true, 0, 4, FallAlert.ACTION_UNANSWERED) == FallAlert.ARM, "timeout during disconnect");
        fresh.disarm();
        expect(kind(fresh, true, false, 0, 4, FallAlert.ACTION_OK) == FallAlert.NONE, "episode closed");
        // A ride started while disconnected restarts counters; its help is new.
        FallAlert gap = new FallAlert();
        kind(gap, false, false, 0, 0, 0);
        gap.reset();
        expect(kind(gap, true, false, 0, 1, FallAlert.ACTION_HELP) == FallAlert.ARM, "help in ride started offline");
        fresh = new FallAlert();
        kind(fresh, true, false, 0, 3, FallAlert.ACTION_HELP);
        // New ride restarts counters at zero without false alarms.
        expect(kind(fresh, false, false, 0, 0, 0) == FallAlert.NONE, "ride ended");
        expect(kind(fresh, true, false, 0, 0, 0) == FallAlert.NONE, "new ride");
        expect(kind(fresh, true, false, 0, 1, FallAlert.ACTION_HELP) == FallAlert.ARM, "help in new ride");

        // Ending the ride during an unanswered countdown means the rider is responsive.
        FallAlert end = new FallAlert();
        kind(end, true, true, 3, 0, 0); kind(end, true, true, 0, 0, 4);
        expect(kind(end, false, false, 0, 0, 4) == FallAlert.CANCEL, "ride end cancels");

        // Plan parsing and the SMS text.
        List<EmergencyPlan.Contact> people = EmergencyPlan.parse("妈妈 138-0000-0000\n+8613900000000\n\n爸爸 13800000000");
        expect(people.size() == 2 && people.get(0).name.equals("妈妈") && people.get(0).number.equals("13800000000"), "parse/dedupe");
        expect(people.get(1).number.equals("13900000000") && people.get(1).name.equals("联系人2"), "+86 and default name");
        try { EmergencyPlan.parse("妈妈 abc"); throw new AssertionError("bad number accepted"); }
        catch (IllegalArgumentException expected) { }
        try { EmergencyPlan.parse("1 110\n2 120\n3 119\n4 12345"); throw new AssertionError("fourth contact accepted"); }
        catch (IllegalArgumentException expected) { }
        expect(EmergencyPlan.parse(EmergencyPlan.format(people)).size() == 2, "format round trip");
        String sms = EmergencyPlan.message(FallAlert.Reason.UNANSWERED, "11-05 10:00", 31.2304, 121.4737, 8.4f, 30);
        expect(sms.contains("position=121.473700,31.230400") && sms.contains("coordinate=wgs84") && sms.contains("精度约 8 米"), sms);
        expect(!sms.contains("分钟前"), "fresh fix has no age");
        String old = EmergencyPlan.message(FallAlert.Reason.HELP, "11-05 10:00", 31.2, 121.4, 20, 600);
        expect(old.contains("需要帮助") && old.contains("10 分钟前"), old);
        String none = EmergencyPlan.message(FallAlert.Reason.DRILL, "11-05 10:00", 0, 0, 0, -1);
        expect(none.contains("演练") && none.contains("暂未取得定位") && !none.contains("请尽快"), none);
        expect(EmergencyPlan.countdown(FallAlert.Reason.HELP) < EmergencyPlan.countdown(FallAlert.Reason.UNANSWERED), "help is faster");
        System.out.println("FallAlertTest passed");
    }
}
