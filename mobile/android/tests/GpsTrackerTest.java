package com.velaride.companion;

public final class GpsTrackerTest {
    private static long ns(long seconds) { return seconds * 1_000_000_000L; }
    private static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    public static void main(String[] args) {
        GpsTracker tracker = new GpsTracker();
        GpsTracker.Sample sample = tracker.accept(ns(10), ns(10), 5, 0, true, 5);
        check(sample != null && sample.speed == 18 && sample.distance == 0);
        sample = tracker.accept(ns(11), ns(11), 5, 5, true, 5);
        check(sample != null && sample.distance == .005);
        check(tracker.accept(ns(11), ns(11), 5, 5, true, 5) == null);
        check(tracker.accept(ns(12), ns(10), 5, 5, true, 5) == null);
        check(tracker.accept(ns(12), ns(13), 5, 5, true, 5) == null);
        check(tracker.accept(ns(30), ns(12), 5, 5, true, 5) == null);
        for (double invalid : new double[]{-1, 36, Double.NaN, Double.POSITIVE_INFINITY})
            check(tracker.accept(ns(12), ns(12), invalid, 5, true, 5) == null);
        check(tracker.accept(ns(12), ns(12), 5, 1000, true, 5) == null);
        check(tracker.accept(ns(12), ns(12), 5, 5, true, Double.NaN) == null);
        sample = tracker.accept(ns(12), ns(12), 5, 5, false, 0);
        check(sample != null && sample.speed == 18 && sample.distance == .01);
        sample = tracker.accept(ns(13), ns(13), 5, 3, true, 0);
        check(sample != null && sample.speed == 0 && sample.distance == .01);
        sample = tracker.accept(ns(40), ns(40), 5, 1000, false, 0);
        check(sample != null && sample.speed == 0 && sample.distance == .01);
        sample = tracker.accept(ns(41), ns(41), 5, 5, true, 5);
        check(sample != null && Math.abs(sample.distance - .015) < 1e-9);
        check(new GpsTracker().accept(ns(42), ns(42), 5, 0, true, 0).distance == 0);
        System.out.println("PASS: GPS accuracy/age/time validation, duplicate/out-of-order rejection, jump filtering, signal gaps, stationary and restart");
    }
}
