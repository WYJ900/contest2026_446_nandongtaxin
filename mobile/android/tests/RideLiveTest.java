package com.velaride.companion;

public final class RideLiveTest {
    private static void expect(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    private static RideLive ride(boolean active, boolean paused, long elapsed, boolean pending, long at) {
        return new RideLive(active, paused, 2, elapsed, 30, 5, 1, 0, 0, false, 0, 0, 0, pending, 21, at);
    }

    public static void main(String[] args) {
        RideLive moving = ride(true, false, 100, false, 50_000);
        expect(moving.elapsedAt(53_500) == 103, "clock advances between polls");
        expect(moving.headline(53_500).equals("骑行中  00:01:43"), "headline " + moving.headline(53_500));
        expect(moving.elapsedAt(70_000) == 100, "stale data freezes the clock");
        expect(moving.detail(70_000).contains("超过 10 秒"), "stale data is labelled");
        expect(moving.elapsedAt(40_000) == 100, "clock never runs backwards");

        RideLive paused = ride(true, true, 100, false, 50_000);
        expect(paused.elapsedAt(55_000) == 100 && paused.headline(55_000).startsWith("已暂停"), "paused clock frozen");

        RideLive idle = ride(false, false, 0, true, 50_000);
        expect(idle.headline(50_000).equals("手表待机中") && idle.safety().isEmpty(), "idle shows no alert");
        expect(ride(true, false, 5, true, 0).safety().contains("21 秒"), "pending safety check shown");

        RideLive gps = new RideLive(true, false, 2, 10, 10, 0, 0, 0, 0, true, 18.26, 3.456, 0, false, 0, 0);
        expect(gps.detail(0).contains("18.3 km/h · 3.46 km"), "phone speed shown " + gps.detail(0));
        RideLive bad = new RideLive(true, false, 2, 10, 10, 0, 0, 0, 0, true, Double.NaN, 1, 1200, false, 0, 0);
        expect(!bad.phoneValid && bad.detail(0).contains("定位距离 1.20 km"), "NaN speed rejected");
        expect(RideLive.clock(3725).equals("01:02:05") && RideLive.clock(-4).equals("00:00:00"), "clock format");
        System.out.println("RideLiveTest passed");
    }
}
