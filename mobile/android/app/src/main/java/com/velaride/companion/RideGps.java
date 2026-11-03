package com.velaride.companion;

/** Recorded ride metrics; zero coverage means unavailable, not zero distance. */
public final class RideGps {
    public static final RideGps NONE = new RideGps(0, 0, 0, 0);
    public final long timeMs, distanceM, avgCenti, maxCenti;
    public RideGps(long timeMs, long distanceM, long avgCenti, long maxCenti) {
        if (timeMs < 0 || timeMs > 0xffffffffL || distanceM < 0 || distanceM > 0xffffffffL
                || avgCenti < 0 || avgCenti > 20000 || maxCenti < 0 || maxCenti > 20000
                || (timeMs == 0 && (distanceM != 0 || avgCenti != 0 || maxCenti != 0)))
            throw new IllegalArgumentException("骑行 GPS 数据异常");
        this.timeMs = timeMs; this.distanceM = distanceM; this.avgCenti = avgCenti; this.maxCenti = maxCenti;
    }
    public boolean available() { return timeMs > 0; }
}
