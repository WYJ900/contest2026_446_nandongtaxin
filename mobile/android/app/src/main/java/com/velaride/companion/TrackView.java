package com.velaride.companion;

import android.content.Context;
import android.graphics.*;
import android.view.View;
import java.util.ArrayList;
import java.util.List;

/** Draws the walked GPS track (local metres, north up) with start, current position, parking spot and route. */
final class TrackView extends View {
    /** Track points survive Activity recreation for the whole app process. */
    static final List<double[]> TRACK = new ArrayList<>();
    static final int MAX_POINTS = 5000;

    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG), dot = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint route = new Paint(Paint.ANTI_ALIAS_FLAG), label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint grid = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int green, ink, muted, orange;
    private List<RoutePlan.Point> routePoints = new ArrayList<>();
    private double[] parking;

    TrackView(Context context, int green, int ink, int muted) {
        super(context);
        this.green = green; this.ink = ink; this.muted = muted; this.orange = Color.rgb(255, 176, 92);
        float d = getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE); line.setStrokeWidth(4 * d); line.setColor(green);
        line.setStrokeCap(Paint.Cap.ROUND); line.setStrokeJoin(Paint.Join.ROUND);
        route.setStyle(Paint.Style.STROKE); route.setStrokeWidth(2 * d); route.setColor(muted);
        route.setPathEffect(new DashPathEffect(new float[]{8 * d, 6 * d}, 0));
        grid.setStyle(Paint.Style.STROKE); grid.setStrokeWidth(1); grid.setColor(Color.argb(40, 255, 255, 255));
        label.setTextSize(12 * d); label.setColor(muted);
    }

    /** Adds a fix if it moved at least 3 m; returns the track length in metres. */
    static synchronized double add(double lat, double lon) {
        if (!TRACK.isEmpty()) {
            double[] last = TRACK.get(TRACK.size() - 1);
            if (metres(last[0], last[1], lat, lon) < 3) return length();
        }
        if (TRACK.size() >= MAX_POINTS) TRACK.remove(0);
        TRACK.add(new double[]{lat, lon});
        return length();
    }
    static synchronized double length() {
        double total = 0;
        for (int i = 1; i < TRACK.size(); i++) total += metres(TRACK.get(i - 1)[0], TRACK.get(i - 1)[1], TRACK.get(i)[0], TRACK.get(i)[1]);
        return total;
    }
    static synchronized List<RoutePlan.Point> asRoute() {
        List<RoutePlan.Point> out = new ArrayList<>();
        for (double[] p : TRACK) out.add(new RoutePlan.Point(p[0], p[1]));
        return out;
    }
    static synchronized void clear() { TRACK.clear(); }
    static double metres(double lat1, double lon1, double lat2, double lon2) {
        double dy = (lat2 - lat1) * 111320.0, dx = (lon2 - lon1) * 111320.0 * Math.cos(Math.toRadians((lat1 + lat2) / 2));
        return Math.hypot(dx, dy);
    }

    void setRoute(List<RoutePlan.Point> points) { routePoints = points == null ? new ArrayList<>() : points; invalidate(); }
    void setParking(double[] latLon) { parking = latLon; invalidate(); }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int w = MeasureSpec.getSize(widthSpec);
        setMeasuredDimension(w, Math.round(w * 0.75f));
    }

    @Override protected void onDraw(Canvas canvas) {
        float d = getResources().getDisplayMetrics().density;
        float w = getWidth(), h = getHeight(), pad = 18 * d;
        for (int i = 1; i < 4; i++) { canvas.drawLine(w * i / 4, 0, w * i / 4, h, grid); canvas.drawLine(0, h * i / 4, w, h * i / 4, grid); }
        List<double[]> track;
        synchronized (TrackView.class) { track = new ArrayList<>(TRACK); }
        List<double[]> all = new ArrayList<>(track);
        for (RoutePlan.Point p : routePoints) all.add(new double[]{p.lat, p.lon});
        if (parking != null) all.add(parking);
        if (all.isEmpty()) {
            label.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("开启定位同步后，走过的路线会画在这里", w / 2, h / 2, label);
            return;
        }
        double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
        for (double[] p : all) { minLat = Math.min(minLat, p[0]); maxLat = Math.max(maxLat, p[0]); minLon = Math.min(minLon, p[1]); maxLon = Math.max(maxLon, p[1]); }
        double midLat = (minLat + maxLat) / 2, kx = 111320.0 * Math.cos(Math.toRadians(midLat)), ky = 111320.0;
        double spanX = Math.max(40, (maxLon - minLon) * kx), spanY = Math.max(40, (maxLat - minLat) * ky);
        double scale = Math.min((w - 2 * pad) / spanX, (h - 2 * pad) / spanY);
        double cx = (minLon + maxLon) / 2, cy = midLat;
        Projector at = (lat, lon) -> new PointF((float) (w / 2 + (lon - cx) * kx * scale), (float) (h / 2 - (lat - cy) * ky * scale));

        if (routePoints.size() > 1) {
            Path path = new Path();
            for (int i = 0; i < routePoints.size(); i++) {
                PointF p = at.map(routePoints.get(i).lat, routePoints.get(i).lon);
                if (i == 0) path.moveTo(p.x, p.y); else path.lineTo(p.x, p.y);
            }
            canvas.drawPath(path, route);
        }
        if (track.size() > 1) {
            Path path = new Path();
            for (int i = 0; i < track.size(); i++) {
                PointF p = at.map(track.get(i)[0], track.get(i)[1]);
                if (i == 0) path.moveTo(p.x, p.y); else path.lineTo(p.x, p.y);
            }
            canvas.drawPath(path, line);
        }
        if (!track.isEmpty()) {
            PointF start = at.map(track.get(0)[0], track.get(0)[1]);
            dot.setColor(green); canvas.drawCircle(start.x, start.y, 6 * d, dot);
            PointF now = at.map(track.get(track.size() - 1)[0], track.get(track.size() - 1)[1]);
            dot.setColor(ink); canvas.drawCircle(now.x, now.y, 7 * d, dot);
            dot.setColor(green); canvas.drawCircle(now.x, now.y, 4 * d, dot);
        }
        if (parking != null) {
            PointF p = at.map(parking[0], parking[1]);
            dot.setColor(orange); canvas.drawCircle(p.x, p.y, 9 * d, dot);
            label.setColor(Color.BLACK); label.setTextAlign(Paint.Align.CENTER); label.setFakeBoldText(true);
            canvas.drawText("P", p.x, p.y + 4 * d, label);
            label.setFakeBoldText(false); label.setColor(muted);
        }
        // Scale bar.
        double metres = spanX > 400 ? 100 : spanX > 150 ? 50 : 20;
        float bar = (float) (metres * scale), y = h - 8 * d;
        line.setStrokeWidth(2 * d); line.setColor(muted);
        canvas.drawLine(pad, y, pad + bar, y, line);
        line.setStrokeWidth(4 * d); line.setColor(green);
        label.setTextAlign(Paint.Align.LEFT);
        canvas.drawText(Math.round(metres) + " 米   ↑北", pad + bar + 6 * d, y + 4 * d, label);
    }

    private interface Projector { PointF map(double lat, double lon); }
}
