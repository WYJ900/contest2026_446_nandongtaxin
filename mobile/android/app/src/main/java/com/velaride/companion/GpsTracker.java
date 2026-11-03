package com.velaride.companion;

/** Validates foreground GPS samples using monotonic fix time, never wall time. */
public final class GpsTracker {
    public static final long MAX_AGE_NS = 15_000_000_000L;
    private long lastFix;
    private double distance;
    public static final class Sample {
        public final double speed, distance;
        Sample(double speed, double distance) { this.speed = speed; this.distance = distance; }
    }
    public Sample accept(long now, long fix, double accuracy, double deltaMeters, boolean hasSpeed, double metersPerSecond) {
        if (fix <= 0 || fix > now || now - fix >= MAX_AGE_NS || fix <= lastFix
                || !Double.isFinite(accuracy) || accuracy < 0 || accuracy > 35
                || !Double.isFinite(deltaMeters) || deltaMeters < 0) return null;
        double seconds = lastFix == 0 ? 0 : (fix - lastFix) / 1e9;
        boolean continuous = seconds > 0 && seconds < 15;
        double inferred = continuous ? deltaMeters / seconds * 3.6 : 0;
        double speed = hasSpeed ? metersPerSecond * 3.6 : inferred;
        if (!Double.isFinite(speed) || speed < 0 || speed > 150 || inferred > 150) return null;
        if (continuous && speed >= 1.8) distance += deltaMeters / 1000;
        lastFix = fix;
        return new Sample(speed, distance);
    }
}
