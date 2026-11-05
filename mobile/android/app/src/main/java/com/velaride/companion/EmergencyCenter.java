package com.velaride.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationManager;
import android.media.AudioAttributes;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.*;
import android.telecom.TelecomManager;
import android.telephony.SmsManager;
import org.json.JSONObject;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Phone-side escalation after the watch's own safety confirmation.
 * Countdown, alarm, SMS and call all run here so they work while the
 * activity is closed; EmergencyActivity is only a view of this state.
 */
@SuppressLint("MissingPermission")
final class EmergencyCenter {
    interface Listener { void changed(); }
    static final int NOTIFY_ID = 42;
    private static final String CHANNEL = "fall_alert", SENT = "com.velaride.companion.SMS_SENT";
    private static EmergencyCenter instance;
    static synchronized EmergencyCenter get(Context context) {
        if (instance == null) instance = new EmergencyCenter(context.getApplicationContext());
        return instance;
    }

    final FallAlert alert = new FallAlert();
    private final Context context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private Ringtone ringtone;
    private FallAlert.Reason reason;
    private long deadline;
    private boolean drill;
    private volatile String status = "未触发";
    private volatile String watchNote = "";
    private Location fix;
    private int smsPending, smsOk, smsFailed;

    private EmergencyCenter(Context context) {
        this.context = context;
        IntentFilter filter = new IntentFilter(SENT);
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent intent) {
                if (getResultCode() == Activity.RESULT_OK) smsOk++; else smsFailed++;
                smsPending--;
                if (smsPending <= 0) {
                    status = "短信：成功 " + smsOk + " 条" + (smsFailed > 0 ? "，失败 " + smsFailed + " 条（请检查短信权限/SIM）" : "")
                            + "\n" + callText;
                    notifyState(false); changed();
                }
            }
        };
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        else context.registerReceiver(receiver, filter);
    }
    private String callText = "";

    SharedPreferences prefs() { return context.getSharedPreferences("emergency", Context.MODE_PRIVATE); }
    boolean enabled() { return prefs().getBoolean("enabled", false); }
    List<EmergencyPlan.Contact> contacts() {
        try { return EmergencyPlan.parse(prefs().getString("contacts", "")); }
        catch (IllegalArgumentException error) { return Collections.emptyList(); }
    }
    void add(Listener listener) { listeners.addIfAbsent(listener); }
    void remove(Listener listener) { listeners.remove(listener); }
    private void changed() { for (Listener l : listeners) l.changed(); }

    boolean counting() { return reason != null && deadline > 0; }
    FallAlert.Reason reason() { return reason; }
    boolean drill() { return drill; }
    int secondsLeft() { return counting() ? (int) Math.max(0, (deadline - SystemClock.elapsedRealtime() + 999) / 1000) : 0; }
    String status() { return status; }
    String watchNote() { return watchNote; }

    /** Remember the last good phone GPS fix so a later SMS still has a position. */
    void rememberFix(Location location) {
        prefs().edit().putLong("lat_e7", Math.round(location.getLatitude() * 1e7))
                .putLong("lon_e7", Math.round(location.getLongitude() * 1e7))
                .putFloat("acc", location.getAccuracy()).putLong("fix_ms", System.currentTimeMillis()).apply();
    }

    /** Called from the BLE worker with each ride_status reply. */
    void onStatus(JSONObject s) {
        boolean supported = s.has("safety_pending") && s.has("safety_help");
        FallAlert.Decision d = alert.update(supported, s.optBoolean("active"), s.optBoolean("safety_pending"),
                s.optInt("safety_remaining_s"), s.optLong("safety_help"), s.optInt("safety_last_action"));
        main.post(() -> apply(d));
    }
    void onDisconnected() {
        main.post(() -> {
            alert.reset();
            if (counting() && !drill) { watchNote = "手表连接已中断，倒计时继续；如安全请点取消。"; changed(); }
        });
    }

    private void apply(FallAlert.Decision d) {
        String note = watchNote;
        if (d.kind == FallAlert.UNSUPPORTED) watchNote = "当前手表固件不提供安全确认数据，摔倒呼救不可用。";
        else if (d.kind == FallAlert.WARN) {
            watchNote = "手表检测到疑似摔倒，正在等待腕上确认（" + d.remaining + " 秒）";
            if (enabled() && !counting()) notifyWarning(d.remaining);
        } else if (d.kind == FallAlert.ARM) {
            if (!enabled()) { alert.disarm(); watchNote = "手表请求帮助，但摔倒呼救未开启。"; }
            else arm(d.reason, false);
        } else if (d.kind == FallAlert.CANCEL) {
            if (counting() && !drill) cancel("手表上已确认安全，呼救已自动取消。");
        } else if (!counting()) watchNote = "";
        if (d.kind != FallAlert.WARN && !counting() && d.kind != FallAlert.ARM) clearWarning();
        if (!note.equals(watchNote)) changed();
    }

    void arm(FallAlert.Reason why, boolean asDrill) {
        if (counting()) return;
        reason = why; drill = asDrill; smsOk = smsFailed = smsPending = 0; callText = ""; warningShown = false;
        deadline = SystemClock.elapsedRealtime() + EmergencyPlan.countdown(why) * 1000L;
        status = asDrill ? "演练倒计时中，不会发送短信或拨号" : "倒计时结束后将发送短信并拨打电话";
        watchNote = why == FallAlert.Reason.HELP ? "骑行者在手表上选择了“需要帮助”" :
                why == FallAlert.Reason.DRILL ? "摔倒呼救演练" : "疑似摔倒且 30 秒未在手表上确认";
        fix = null; requestLocation();
        startAlarm(); notifyState(true); openScreen(); changed();
        main.removeCallbacks(tick); main.post(tick);
    }

    void cancel(String why) {
        if (!counting()) return;
        main.removeCallbacks(tick); stopAlarm();
        deadline = 0; reason = null; alert.disarm(); status = why; watchNote = "";
        notifyState(false); changed();
    }

    void sendNow() { if (counting()) { deadline = SystemClock.elapsedRealtime(); main.removeCallbacks(tick); main.post(tick); } }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!counting()) return;
            if (SystemClock.elapsedRealtime() >= deadline) { dispatch(); return; }
            notifyState(false); changed();
            main.postDelayed(this, 500);
        }
    };

    private void dispatch() {
        FallAlert.Reason why = reason; boolean wasDrill = drill;
        deadline = 0; reason = null; alert.disarm(); stopAlarm();
        List<EmergencyPlan.Contact> people = contacts();
        String text = smsText(wasDrill ? FallAlert.Reason.DRILL : why);
        if (wasDrill) {
            status = "演练完成（未发送）。真实情况下将发送给 " + people.size() + " 位联系人：\n" + text;
            notifyState(false); changed(); return;
        }
        if (people.isEmpty()) { status = "未设置紧急联系人，无法自动求助！请立即手动联系他人。"; notifyState(false); changed(); return; }
        sendSms(people, text);
        callText = call(people.get(0));
        if (smsPending <= 0) status = (smsFailed > 0 ? "短信未能发出（请检查短信权限/SIM）" : "短信已提交") + "\n" + callText;
        else status = "正在发送短信给 " + people.size() + " 位联系人…\n" + callText;
        notifyState(false); changed();
    }

    /** Explicit user test: a clearly-marked drill SMS, no call. */
    void sendTest() {
        if (counting()) return;
        List<EmergencyPlan.Contact> people = contacts();
        if (people.isEmpty()) { status = "请先保存紧急联系人。"; changed(); return; }
        smsOk = smsFailed = smsPending = 0; callText = "（测试不拨号）";
        fix = null; requestLocation();
        main.postDelayed(() -> {
            sendSms(people, smsText(FallAlert.Reason.DRILL));
            status = smsPending > 0 ? "正在发送测试短信…" : "测试短信未能发出，请检查短信权限/SIM 卡。";
            changed();
        }, 1500);
    }

    String smsText(FallAlert.Reason why) {
        String clock = new SimpleDateFormat("MM-dd HH:mm", Locale.CHINA).format(new Date());
        Location best = fix;
        SharedPreferences p = prefs();
        if (best == null && p.contains("fix_ms")) {
            best = new Location("saved");
            best.setLatitude(p.getLong("lat_e7", 0) / 1e7); best.setLongitude(p.getLong("lon_e7", 0) / 1e7);
            best.setAccuracy(p.getFloat("acc", 0)); best.setTime(p.getLong("fix_ms", 0));
        }
        if (best == null) return EmergencyPlan.message(why, clock, 0, 0, 0, -1);
        long age = Math.max(0, (System.currentTimeMillis() - best.getTime()) / 1000);
        return EmergencyPlan.message(why, clock, best.getLatitude(), best.getLongitude(), best.getAccuracy(), age);
    }

    private void sendSms(List<EmergencyPlan.Contact> people, String text) {
        if (context.checkSelfPermission(Manifest.permission.SEND_SMS) != PackageManager.PERMISSION_GRANTED) {
            smsFailed = people.size(); return;
        }
        SmsManager sms;
        try {
            int sub = SmsManager.getDefaultSmsSubscriptionId();
            if (Build.VERSION.SDK_INT >= 31) {
                sms = context.getSystemService(SmsManager.class);
                if (sub != -1) sms = sms.createForSubscriptionId(sub);
            } else sms = sub != -1 ? SmsManager.getSmsManagerForSubscriptionId(sub) : SmsManager.getDefault();
        } catch (RuntimeException error) { smsFailed = people.size(); return; }
        ArrayList<String> parts = sms.divideMessage(text);
        int request = 0;
        for (EmergencyPlan.Contact person : people) {
            ArrayList<PendingIntent> sent = new ArrayList<>();
            for (int i = 0; i < parts.size(); i++) sent.add(i == parts.size() - 1 ?
                    PendingIntent.getBroadcast(context, 100 + request++, new Intent(SENT).setPackage(context.getPackageName()),
                            PendingIntent.FLAG_IMMUTABLE) : null);
            try { smsPending++; sms.sendMultipartTextMessage(person.number, null, parts, sent, null); }
            catch (RuntimeException error) { smsPending--; smsFailed++; }
        }
    }

    private String call(EmergencyPlan.Contact person) {
        Uri number = Uri.fromParts("tel", person.number, null);
        if (context.checkSelfPermission(Manifest.permission.CALL_PHONE) == PackageManager.PERMISSION_GRANTED) {
            try {
                context.getSystemService(TelecomManager.class).placeCall(number, new Bundle());
                return "已拨打 " + person.name + "（" + person.number + "）";
            } catch (RuntimeException ignored) { }
        }
        try {
            context.startActivity(new Intent(Intent.ACTION_DIAL, number).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return "未获直接拨号权限，已打开拨号盘：" + person.name;
        } catch (RuntimeException error) { return "无法自动拨号，请手动拨打 " + person.number; }
    }

    private void requestLocation() {
        if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) return;
        LocationManager manager = context.getSystemService(LocationManager.class);
        if (manager == null) return;
        try {
            for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                Location known = manager.getLastKnownLocation(provider);
                if (known != null && (fix == null || known.getTime() > fix.getTime())) fix = known;
            }
            if (Build.VERSION.SDK_INT >= 30) {
                CancellationSignal stop = new CancellationSignal();
                main.postDelayed(stop::cancel, 12000);
                manager.getCurrentLocation(LocationManager.GPS_PROVIDER, stop, context.getMainExecutor(), location -> {
                    if (location != null) { fix = location; rememberFix(location); }
                });
            }
        } catch (RuntimeException ignored) { /* Background location may be refused; saved fix remains. */ }
    }

    private void startAlarm() {
        try {
            Uri tone = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM);
            if (tone == null) tone = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE);
            ringtone = RingtoneManager.getRingtone(context, tone);
            if (ringtone != null) {
                ringtone.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build());
                if (Build.VERSION.SDK_INT >= 28) ringtone.setLooping(true);
                ringtone.play();
            }
        } catch (RuntimeException ignored) { }
        try {
            Vibrator vibrator = context.getSystemService(Vibrator.class);
            if (vibrator != null) vibrator.vibrate(VibrationEffect.createWaveform(new long[]{0, 600, 400}, 0));
        } catch (RuntimeException ignored) { }
    }
    private void stopAlarm() {
        try { if (ringtone != null) ringtone.stop(); } catch (RuntimeException ignored) { }
        ringtone = null;
        try { Vibrator v = context.getSystemService(Vibrator.class); if (v != null) v.cancel(); } catch (RuntimeException ignored) { }
    }

    private NotificationManager notifications() {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        NotificationChannel channel = new NotificationChannel(CHANNEL, "摔倒呼救", NotificationManager.IMPORTANCE_HIGH);
        channel.setSound(null, null); channel.enableVibration(false); channel.setBypassDnd(true);
        channel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
        manager.createNotificationChannel(channel);
        return manager;
    }
    private PendingIntent screen(String action, int code) {
        Intent intent = new Intent(context, EmergencyActivity.class).setAction(action)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(context, code, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }
    private void notifyState(boolean fullScreen) {
        try {
            Notification.Builder b = new Notification.Builder(context, CHANNEL).setSmallIcon(R.drawable.ic_launcher)
                    .setCategory(Notification.CATEGORY_ALARM).setVisibility(Notification.VISIBILITY_PUBLIC)
                    .setContentIntent(screen("open", 0)).setOnlyAlertOnce(!fullScreen);
            if (counting()) {
                b.setContentTitle((drill ? "【演练】" : "") + secondsLeft() + " 秒后自动求助").setContentText(watchNote).setOngoing(true)
                        .addAction(new Notification.Action.Builder(null, "我没事，取消", screen("cancel", 1)).build())
                        .addAction(new Notification.Action.Builder(null, "立即求助", screen("send", 2)).build());
                if (fullScreen) b.setFullScreenIntent(screen("open", 3), true);
            } else b.setContentTitle("VelaRide 摔倒呼救").setContentText(status).setStyle(new Notification.BigTextStyle().bigText(status)).setAutoCancel(true);
            notifications().notify(NOTIFY_ID, b.build());
        } catch (RuntimeException ignored) { /* Notifications refused: the service keeps the countdown anyway. */ }
    }
    private boolean warningShown;
    private void notifyWarning(int remaining) {
        try {
            warningShown = true;
            notifications().notify(NOTIFY_ID, new Notification.Builder(context, CHANNEL).setSmallIcon(R.drawable.ic_launcher)
                    .setCategory(Notification.CATEGORY_ALARM).setContentTitle("手表检测到疑似摔倒")
                    .setContentText("等待腕上确认，剩余 " + remaining + " 秒；未确认将自动求助").setOnlyAlertOnce(true)
                    .setContentIntent(screen("open", 0)).build());
        } catch (RuntimeException ignored) { }
    }
    private void clearWarning() {
        if (!warningShown) return;
        warningShown = false;
        try { context.getSystemService(NotificationManager.class).cancel(NOTIFY_ID); } catch (RuntimeException ignored) { }
    }
    private void openScreen() {
        try { context.startActivity(new Intent(context, EmergencyActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); }
        catch (RuntimeException ignored) { /* Background start blocked: the full-screen notification shows it. */ }
    }
}
