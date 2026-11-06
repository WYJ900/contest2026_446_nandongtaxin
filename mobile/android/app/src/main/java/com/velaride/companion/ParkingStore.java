package com.velaride.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.*;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationManager;
import android.os.*;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Saved parking spot. Captured when a ride ends on the watch (service poll)
 * or on request; one GPS fix, falling back to a recent last-known position.
 */
@SuppressLint("MissingPermission")
final class ParkingStore {
    interface Done { void result(String message); }
    static final float MAX_ACCURACY = 100;
    private static final long MAX_AGE_MS = 3 * 60 * 1000;
    private static final CopyOnWriteArrayList<Runnable> listeners = new CopyOnWriteArrayList<>();
    private ParkingStore() { }

    static void add(Runnable listener) { listeners.addIfAbsent(listener); }
    static void remove(Runnable listener) { listeners.remove(listener); }
    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("parking", Context.MODE_PRIVATE);
    }
    static ParkingSpot load(Context context) { return ParkingSpot.decode(prefs(context).getString("spot", "")); }
    static String status(Context context) { return prefs(context).getString("status", ""); }
    static void clear(Context context) { prefs(context).edit().clear().apply(); changed(); }
    private static void changed() { for (Runnable listener : listeners) listener.run(); }

    static void save(Context context, Location location, boolean auto) {
        ParkingSpot spot = new ParkingSpot(location.getLatitude(), location.getLongitude(),
                location.hasAccuracy() ? location.getAccuracy() : MAX_ACCURACY, System.currentTimeMillis(), auto);
        prefs(context).edit().putString("spot", spot.encode()).putString("status", "").apply();
        changed();
    }

    /** Main thread. A fresh fix replaces the saved spot; failure keeps the old one. */
    static void capture(Context context, boolean auto, Done done) {
        Context app = context.getApplicationContext();
        if (app.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            fail(app, auto, "未授予精确定位，无法记录停车点。", done); return;
        }
        LocationManager manager = app.getSystemService(LocationManager.class);
        if (manager == null || !manager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            fail(app, auto, "系统定位未开启，未能记录停车点。", done); return;
        }
        Handler main = new Handler(Looper.getMainLooper());
        boolean[] finished = {false};
        Runnable fallback = () -> {
            if (finished[0]) return;
            finished[0] = true;
            Location recent = recent(manager);
            if (recent != null) { save(app, recent, auto); report(done, "已记录停车点（使用最近定位）"); }
            else fail(app, auto, "暂时没有可靠定位，未能记录停车点；可到开阔处手动记录。", done);
        };
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                CancellationSignal stop = new CancellationSignal();
                main.postDelayed(() -> { stop.cancel(); fallback.run(); }, 20000);
                manager.getCurrentLocation(LocationManager.GPS_PROVIDER, stop, app.getMainExecutor(), location -> {
                    if (finished[0]) return;
                    if (location == null || !location.hasAccuracy() || location.getAccuracy() > MAX_ACCURACY) { fallback.run(); return; }
                    finished[0] = true;
                    save(app, location, auto);
                    report(done, "已记录停车点 · 精度约 " + Math.round(location.getAccuracy()) + " 米");
                });
            } else fallback.run();
        } catch (RuntimeException refused) { fallback.run(); }
    }

    private static Location recent(LocationManager manager) {
        Location best = null;
        for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
            try {
                Location known = manager.getLastKnownLocation(provider);
                if (known == null || !known.hasAccuracy() || known.getAccuracy() > MAX_ACCURACY) continue;
                long age = (SystemClock.elapsedRealtimeNanos() - known.getElapsedRealtimeNanos()) / 1000000;
                if (age < 0 || age > MAX_AGE_MS) continue;
                if (best == null || known.getAccuracy() < best.getAccuracy()) best = known;
            } catch (RuntimeException ignored) { }
        }
        return best;
    }
    private static void fail(Context app, boolean auto, String why, Done done) {
        if (auto) { prefs(app).edit().putString("status", "骑行结束时" + why).apply(); changed(); }
        report(done, why);
    }
    private static void report(Done done, String message) { if (done != null) done.result(message); }
}
