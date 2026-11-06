package com.velaride.companion;

import java.util.Locale;

/**
 * Where the bike was left and how to walk back to it. Raw GPS (WGS-84)
 * straight-line guidance only; no map or road routing. Pure Java for tests.
 */
final class ParkingSpot {
    static final int UNKNOWN = -1, STRAIGHT = 0, LEFT = 1, RIGHT = 2, BEHIND = 3, ARRIVED = 4;
    final double lat, lon;
    final float accuracy;
    final long savedMs;
    final boolean auto;

    ParkingSpot(double lat, double lon, float accuracy, long savedMs, boolean auto) {
        if (!Double.isFinite(lat) || !Double.isFinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180
                || (lat == 0 && lon == 0)) throw new IllegalArgumentException("停车点坐标异常");
        if (!Float.isFinite(accuracy) || accuracy < 0) throw new IllegalArgumentException("停车点精度异常");
        this.lat = lat; this.lon = lon; this.accuracy = accuracy; this.savedMs = savedMs; this.auto = auto;
    }

    String encode() {
        return String.format(Locale.ROOT, "%.7f,%.7f,%.1f,%d,%d", lat, lon, accuracy, savedMs, auto ? 1 : 0);
    }
    /** Returns null for an empty or damaged preference instead of throwing. */
    static ParkingSpot decode(String value) {
        if (value == null || value.isEmpty()) return null;
        try {
            String[] p = value.split(",");
            if (p.length != 5) return null;
            return new ParkingSpot(Double.parseDouble(p[0]), Double.parseDouble(p[1]), Float.parseFloat(p[2]),
                    Long.parseLong(p[3]), "1".equals(p[4]));
        } catch (IllegalArgumentException damaged) { return null; }
    }

    static final class Guide {
        final long metres;
        final double bearing, relative;
        final int direction;
        Guide(long metres, double bearing, double relative, int direction) {
            this.metres = metres; this.bearing = bearing; this.relative = relative; this.direction = direction;
        }
        boolean arrived() { return direction == ARRIVED; }
        String text() {
            if (arrived()) return "已到停车点附近，请四处看看";
            String turn = direction == STRAIGHT ? "前方" : direction == LEFT ? "左手边" : direction == RIGHT ? "右手边"
                    : direction == BEHIND ? "身后" : cardinal(bearing) + "方向";
            return turn + " · 直线约 " + distance(metres);
        }
    }

    /**
     * heading is where the rider faces in degrees from true north, or NaN when
     * neither GPS course nor the phone compass is trustworthy.
     */
    Guide guide(double fromLat, double fromLon, double fixAccuracy, double heading) {
        double metres = distance(fromLat, fromLon, lat, lon);
        double bearing = bearing(fromLat, fromLon, lat, lon);
        double radius = Math.max(15, Math.min(40, Math.max(fixAccuracy, accuracy)));
        if (metres <= radius) return new Guide(Math.round(metres), bearing, 0, ARRIVED);
        if (!Double.isFinite(heading)) return new Guide(Math.round(metres), bearing, Double.NaN, UNKNOWN);
        double relative = ((bearing - heading) % 360 + 540) % 360 - 180;
        int direction = Math.abs(relative) > 135 ? BEHIND : relative > 35 ? RIGHT : relative < -35 ? LEFT : STRAIGHT;
        return new Guide(Math.round(metres), bearing, relative, direction);
    }

    static String distance(long metres) {
        return metres >= 1000 ? String.format(Locale.ROOT, "%.1f 公里", metres / 1000.0) : metres + " 米";
    }
    static String cardinal(double bearing) {
        String[] names = {"北", "东北", "东", "东南", "南", "西南", "西", "西北"};
        return names[(int) Math.floor(((bearing % 360 + 360) % 360 + 22.5) / 45) % 8];
    }
    static double distance(double aLat, double aLon, double bLat, double bLon) {
        double dLat = Math.toRadians(bLat - aLat), dLon = Math.toRadians(bLon - aLon);
        double h = Math.sin(dLat / 2) * Math.sin(dLat / 2)
                + Math.cos(Math.toRadians(aLat)) * Math.cos(Math.toRadians(bLat)) * Math.sin(dLon / 2) * Math.sin(dLon / 2);
        return 6371000 * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(Math.max(0, 1 - h)));
    }
    static double bearing(double aLat, double aLon, double bLat, double bLon) {
        double la = Math.toRadians(aLat), lb = Math.toRadians(bLat), dl = Math.toRadians(bLon - aLon);
        double deg = Math.toDegrees(Math.atan2(Math.sin(dl) * Math.cos(lb),
                Math.cos(la) * Math.sin(lb) - Math.sin(la) * Math.cos(lb) * Math.cos(dl)));
        return (deg + 360) % 360;
    }
}
