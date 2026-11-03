package com.velaride.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.location.*;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import org.json.JSONObject;
import org.json.JSONArray;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.DateFormat;
import java.util.*;
import java.util.concurrent.*;

/** Foreground companion: direct watch connection, real records, optional phone GPS. */
@SuppressLint("MissingPermission")
public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(16, 21, 18), CARD = Color.rgb(28, 36, 30);
    private static final int INK = Color.rgb(238, 245, 233), MUTED = Color.rgb(155, 173, 157);
    private static final int GREEN = Color.rgb(183, 237, 114), BLE_PERMISSION = 10, GPS_PERMISSION = 11;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile long session;
    private BleLink link;
    private BluetoothAdapter bluetooth;
    private BluetoothLeScanner scanner;
    private AlertDialog scanDialog;
    private ArrayAdapter<String> deviceNames;
    private final List<BluetoothDevice> devices = new ArrayList<>();
    private SharedPreferences store;
    private TextView state, live, duration, moving, recordDetail, cachedLabel, gpsValue, gpsStatus, message;
    private Button connect, search, sync, refresh, clock, gps, saveNote;
    private EditText note;
    private JSONObject latest;
    private boolean busy, scanning, gpsSending;
    private long scanEpoch;
    private volatile boolean gpsRunning;
    private volatile boolean gpsSupported;
    private long lastGpsSent;
    private double phoneDistance;
    private Location lastLocation;
    private GpsTracker gpsTracker = new GpsTracker();
    private volatile long gpsGeneration;
    private long lastGpsFix;
    private volatile boolean gpsStale;
    private LocationManager locationManager;
    private static final int EXPORT_HISTORY = 12;
    // Activity recreation must share the same serialized store; an old worker
    // may still be finishing an atomic write when a new Activity is created.
    private static RideHistory sharedHistory;
    private RideHistory history;
    private TextView historySummary;
    private Button browseHistory, exportHistory;
    private Button syncArchive;
    private String historyWarning = "";
    private String historyFailure;

    @Override public void onCreate(Bundle stateBundle) {
        super.onCreate(stateBundle);
        store = getSharedPreferences("velaride", MODE_PRIVATE);
        BluetoothManager manager = getSystemService(BluetoothManager.class);
        bluetooth = manager == null ? null : manager.getAdapter();
        locationManager = getSystemService(LocationManager.class);
        link = new BleLink(this, this::connectionChanged);
        try {
            if (sharedHistory == null) sharedHistory = new RideHistory(new HistoryFile(getFilesDir()));
            history = sharedHistory;
        }
        catch (IOException error) { historyFailure = "历史暂时无法读取，原文件已保留：" + error.getMessage(); }
        makeScreen();
        if (stateBundle != null) note.setText(stateBundle.getString("draft", ""));
        try {
            String cached = store.getString("latest_record", "");
            if (!cached.isEmpty()) {
                JSONObject record = new JSONObject(cached); validateRecord(record); showRecord(record, true);
                if (history != null) {
                    RideHistory.Entry migrated = historyEntry(record, store.getString("watch_address", "legacy-unknown"),
                            store.getLong("synced_at", 0), "");
                    boolean alreadySaved = false;
                    for (RideHistory.Entry saved : history.snapshot()) if (saved.sameRide(migrated)) {
                        alreadySaved = (!migrated.gps.available() || saved.gps.available())
                                && (!migrated.safety.available() || saved.safety.available()); break;
                    }
                    if (!alreadySaved) worker.execute(() -> {
                        String failure = null;
                        try { history.upsert(migrated); } catch (IOException error) { failure = error.getMessage(); }
                        final String warning = failure;
                        main.post(() -> {
                            if (isDestroyed()) return;
                            if (warning != null) message.setText(warning); showHistorySummary();
                        });
                    });
                }
            }
        } catch (Exception ignored) { store.edit().remove("latest_record").apply(); }
        updateButtons();
        showHistorySummary();
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private GradientDrawable background(int color, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color); shape.setCornerRadius(dp(radius)); return shape;
    }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setLineSpacing(dp(3), 1); return view;
    }
    private void gap(LinearLayout parent, int height) {
        parent.addView(new View(this), new LinearLayout.LayoutParams(1, dp(height)));
    }
    private LinearLayout card(LinearLayout parent, String title, String subtitle) {
        LinearLayout box = new LinearLayout(this); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(20), dp(20), dp(20)); box.setBackground(background(CARD, 22));
        TextView heading = text(title, 19, INK); heading.setTypeface(null, Typeface.BOLD);
        box.addView(heading); gap(box, 5);
        box.addView(text(subtitle, 13, MUTED)); gap(box, 18);
        parent.addView(box, new LinearLayout.LayoutParams(-1, -2)); gap(parent, 14); return box;
    }
    private Button button(LinearLayout parent, String label, boolean accent, View.OnClickListener action) {
        Button view = new Button(this); view.setText(label); view.setAllCaps(false); view.setTextSize(15);
        view.setTextColor(accent ? BG : GREEN); view.setBackground(background(accent ? GREEN : Color.rgb(42, 54, 42), 13));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(48));
        params.topMargin = dp(8); parent.addView(view, params); view.setOnClickListener(action); return view;
    }
    private void makeScreen() {
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(BG);
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(24), dp(20), dp(24)); scroll.addView(root); setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            int top, bottom;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                top = bars.top; bottom = bars.bottom;
            } else { top = insets.getSystemWindowInsetTop(); bottom = insets.getSystemWindowInsetBottom(); }
            root.setPadding(dp(20), top + dp(20), dp(20), bottom + dp(24)); return insets;
        });
        TextView brand = text("VELARIDE", 13, GREEN); brand.setLetterSpacing(.22f); root.addView(brand);
        gap(root, 9); TextView heading = text("一起，骑得更远。", 30, INK);
        heading.setTypeface(null, Typeface.BOLD); root.addView(heading);
        gap(root, 8); root.addView(text("手机与手表，保持同一步调。", 15, MUTED)); gap(root, 25);

        LinearLayout device = card(root, "我的手表", "通过蓝牙直接连接，无需电脑或网络。");
        state = text("尚未连接", 17, GREEN); device.addView(state);
        live = text("连接后自动校准时间，读取手表状态。", 13, MUTED); gap(device, 7); device.addView(live);
        connect = button(device, "连接手表", true, v -> {
            if (link.isReady()) link.disconnect("已主动断开手表"); else connectWatch(false);
        });
        search = button(device, "搜索附近的 VelaRide", false, v -> connectWatch(true));
        refresh = button(device, "刷新手表状态", false, v -> runJob("正在读取手表…", epoch -> readStatus(epoch)));
        clock = button(device, "同步手机时间", false, v -> runJob("正在校时…", epoch -> syncTime(epoch)));

        LinearLayout record = card(root, "最近一次骑行", "结束手表上的骑行后，在这里同步保存。");
        cachedLabel = text("还没有同步的骑行记录", 12, MUTED); record.addView(cachedLabel); gap(record, 12);
        duration = text("—", 40, INK); duration.setTypeface(Typeface.MONOSPACE); record.addView(duration);
        moving = text("运动时间  —", 15, GREEN); gap(record, 7); record.addView(moving);
        recordDetail = text("暂停  —    休息提醒  —", 13, MUTED); gap(record, 10); record.addView(recordDetail);
        sync = button(record, "同步最新骑行", true, v -> runJob("正在同步骑行记录…", epoch -> readLatest(epoch)));

        LinearLayout archive = card(root, "我的骑行历史", "自动补传手表最近 16 条，手机保存完整历史。旧固件仍只同步最近一条。");
        historySummary = text("还没有保存的骑行", 16, INK); archive.addView(historySummary);
        syncArchive = button(archive, "补传手表全部记录", false,
                v -> runJob("正在补传手表历史…", epoch -> readHistory(epoch)));
        browseHistory = button(archive, "查看全部记录", false, v -> browseHistory());
        exportHistory = button(archive, "导出全部记录 CSV", false, v -> chooseExport());

        LinearLayout position = card(root, "手机定位同步", "开启后将手机速度与本次累计距离发给手表。仅在 App 前台使用。");
        gpsValue = text("0.0 km/h   ·   0.00 km", 23, INK); position.addView(gpsValue);
        gpsStatus = text("尚未开启 · 需要手机位置权限", 13, MUTED); gap(position, 8); position.addView(gpsStatus);
        gps = button(position, "开始定位同步", false, v -> { if (gpsRunning) stopGps(true); else startGps(); });

        LinearLayout memo = card(root, "骑行备注", "把一句简短备注保存到手表的最近一次骑行中。");
        note = new EditText(this); note.setTextColor(INK); note.setHintTextColor(MUTED);
        note.setTextSize(15); note.setHint("例如：今天骑得很舒服，下次继续"); note.setMinLines(2); note.setMaxLines(4);
        memo.addView(note, new LinearLayout.LayoutParams(-1, -2));
        saveNote = button(memo, "保存备注到手表", false, v -> sendNote());
        message = text("数据只在手机与手表之间传输。", 12, MUTED); root.addView(message);
    }

    private void connectionChanged(boolean ready, String reason) {
        if (!ready) { session++; busy = false; gpsSupported = false; stopGps(false); }
        state.setText(reason); state.setTextColor(ready ? GREEN : MUTED);
        updateButtons();
        if (ready) runJob("正在同步设备…", epoch -> {
            boolean supported = WatchHandshake.prepare((command, expected) -> ask(epoch, json(command), expected));
            main.post(() -> {
                if (epoch != session) return;
                gpsSupported = supported;
                gpsStatus.setText(supported ? "尚未开启 · 需要手机位置权限"
                        : "当前手表版本支持校时与骑行同步；定位同步待手表升级后开启。");
                updateButtons();
            });
            syncTime(epoch); readStatus(epoch);
            try { readHistory(epoch); } catch (BleLink.DeviceError error) {
                if (!"no_saved_ride".equals(error.getMessage())) throw error;
            }
        });
    }
    private void updateButtons() {
        boolean ready = link != null && link.isReady();
        connect.setText(ready ? "断开连接" : "连接手表");
        search.setEnabled(!busy); sync.setEnabled(ready && !busy); refresh.setEnabled(ready && !busy);
        clock.setEnabled(ready && !busy); saveNote.setEnabled(ready && !busy && latest != null);
        syncArchive.setEnabled(ready && !busy);
        gps.setEnabled(ready && gpsSupported && (!busy || gpsRunning));
        for (Button action : new Button[]{search, sync, refresh, clock, saveNote, gps, syncArchive})
            action.setAlpha(action.isEnabled() ? 1f : .38f);
    }
    private interface Job { void run(long epoch) throws Exception; }
    private void runJob(String title, Job job) {
        if (busy) return;
        long epoch = session; busy = true; message.setText(title); updateButtons();
        worker.execute(() -> {
            String result = "已完成";
            try { if (epoch == session) job.run(epoch); }
            catch (Exception error) { result = readable(error); }
            String display = result;
            main.post(() -> { if (epoch == session) { busy = false; message.setText(display); updateButtons(); } });
        });
    }
    private String readable(Exception error) {
        String value = error.getMessage();
        if ("no_saved_ride".equals(value)) return "手表还没有已结束的骑行记录。";
        if ("ride_in_progress".equals(value)) return "请先结束手表上的骑行，再保存备注。";
        if ("stale_ride".equals(value)) return "手表记录已更新，请重新同步后再保存备注。";
        return value == null ? "操作失败，请重新连接后重试。" : value;
    }
    private JSONObject json(String command) throws Exception { return new JSONObject().put("cmd", command); }
    private JSONObject ask(long epoch, JSONObject command, String expected) throws Exception {
        if (epoch != session) throw new IOException("连接已经更换，请重新操作");
        return link.request(command, expected);
    }
    private void syncTime(long epoch) throws Exception {
        ask(epoch, json("time_sync").put("epoch", System.currentTimeMillis() / 1000).put("timezone_min", 480), "time_sync");
    }
    private void readStatus(long epoch) throws Exception {
        JSONObject response = ask(epoch, json("ride_status"), "ride_status");
        String summary = response.optBoolean("active") ?
                (response.optBoolean("paused") ? "骑行暂停中" : "正在骑行") + " · " + time(response.optLong("elapsed_s")) : "手表待机中 · 时间已同步";
        if (response.optBoolean("safety_pending")) summary += "\n手表正在等待安全确认，请在腕上处理。";
        else if (response.optBoolean("active") && response.optInt("safety_last_action") == 3)
            summary += "\n已记录求助意向，尚未发送消息；请使用手机联系他人。";
        final String display = summary;
        main.post(() -> { if (epoch == session) live.setText(display); });
    }
    private void readLatest(long epoch) throws Exception {
        String device = store.getString("watch_address", "legacy-unknown");
        JSONObject response = ask(epoch, json("ride_latest"), "ride_latest");
        validateRecord(response);
        if (epoch != session) return;
        long now = System.currentTimeMillis();
        if (history == null) throw new IOException(historyFailure);
        history.upsert(historyEntry(response, device, now, ""));
        if (epoch != session) return;
        store.edit().putString("latest_record", response.toString()).putLong("synced_at", now).apply();
        main.post(() -> { if (epoch == session) showRecord(response, false); });
    }
    private void readHistory(long epoch) throws Exception {
        final String device = store.getString("watch_address", "legacy-unknown");
        final JSONObject index;
        try { index = ask(epoch, json("ride_history"), "ride_history"); }
        catch (BleLink.DeviceError error) {
            if (!"unknown_cmd".equals(error.getMessage())) throw error;
            readLatest(epoch);
            return;
        }
        if (history == null) throw new IOException(historyFailure);
        JSONArray array = index.getJSONArray("sequences");
        final long[] sequences = new long[array.length()];
        for (int i = 0; i < sequences.length; i++) {
            Object value = array.get(i);
            if (!(value instanceof Number)) throw new IOException("手表历史序号格式异常");
            double number = ((Number)value).doubleValue();
            if (!Double.isFinite(number) || number < 0 || number > 4294967295.0 || number != Math.rint(number))
                throw new IOException("手表历史序号异常");
            sequences[i] = ((Number)value).longValue();
        }
        HistorySync.sync(history, device, new HistorySync.Source() {
            public long[] sequences() { return sequences; }
            public RideHistory.Entry read(long sequence) throws Exception {
                JSONObject response = ask(epoch, json("ride_get").put("sequence", sequence), "ride_record");
                validateRecord(response);
                if (epoch != session) throw new IOException("连接已经更换，请重新补传");
                return historyEntry(response, device, System.currentTimeMillis(), "");
            }
        });
        if (sequences.length > 0) readLatest(epoch);
        main.post(() -> {
            if (epoch != session) return;
            historyWarning = index.optInt("corrupt_count") > 0 ? "\n手表有校验失败记录；原数据未删除。" : "";
            showHistorySummary();
        });
    }
    private void showRecord(JSONObject record, boolean cached) {
        latest = record;
        duration.setText(time(record.optLong("elapsed_s")));
        moving.setText("运动时间  " + time(record.optLong("moving_s")));
        recordDetail.setText("暂停  " + record.optInt("pause_count") + " 次    休息提醒  " + record.optInt("rest_count") + " 次\n"
                + "静止  " + time(record.optLong("still_s")) + "    冲击提醒  " + record.optInt("impacts") + " 次\n"
                + rideGpsText(parseRideGps(record)) + "\n\n" + parseRideSafety(record).summary());
        cachedLabel.setText((cached ? "手机已保存" : "刚刚同步") + " · 第 " + record.optLong("sequence") + " 次记录");
        updateButtons();
        showHistorySummary();
    }
    private void validateRecord(JSONObject record) throws Exception {
        for (String key : new String[]{"sequence", "elapsed_s", "moving_s", "still_s", "pause_count", "rest_count", "impacts"}) {
            Object value = record.get(key);
            if (!(value instanceof Number)) throw new IOException("手表记录字段格式异常，请重新同步");
            double number = ((Number)value).doubleValue();
            if (!Double.isFinite(number) || number < 0 || number > 4294967295.0 || number != Math.rint(number))
                throw new IOException("手表记录数值异常，请重新同步");
        }
        if (record.getLong("moving_s") + record.getLong("still_s") > record.getLong("elapsed_s"))
            throw new IOException("手表记录时长不一致，请重新同步");
        parseRideGps(record);
        RideSafety safety = parseRideSafety(record);
        if (safety.lastElapsed > record.getLong("elapsed_s")) throw new IOException("安全确认时间异常");
    }
    private static String time(long seconds) {
        return String.format(Locale.ROOT, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
    private RideHistory.Entry historyEntry(JSONObject record, String device, long syncedAt, String memo) {
        if (device.isEmpty()) device = "legacy-unknown";
        return new RideHistory.Entry(device, record.optLong("sequence"), record.optLong("elapsed_s"),
                record.optLong("moving_s"), record.optLong("still_s"), record.optLong("pause_count"),
                record.optLong("rest_count"), record.optLong("impacts"), syncedAt, syncedAt, memo,
                parseRideGps(record), parseRideSafety(record));
    }
    private RideSafety parseRideSafety(JSONObject record) {
        String[] keys = {"safety_version", "safety_checks", "safety_confirmed", "safety_pauses", "safety_help",
                "safety_unanswered", "safety_last_action", "safety_last_elapsed_s"};
        boolean any = false; for (String key : keys) any |= record.has(key);
        if (!any) return RideSafety.NONE;
        long[] values = new long[keys.length];
        for (int i = 0; i < keys.length; i++) {
            Object value = record.opt(keys[i]);
            if (!(value instanceof Number)) throw new IllegalArgumentException("安全确认字段不完整");
            double number = ((Number)value).doubleValue();
            if (!Double.isFinite(number) || number < 0 || number > 4294967295.0 || number != Math.rint(number))
                throw new IllegalArgumentException("安全确认数值异常");
            values[i] = ((Number)value).longValue();
        }
        return new RideSafety(values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7]);
    }
    private RideGps parseRideGps(JSONObject record) {
        String[] keys = {"gps_time_ms", "gps_distance_m", "gps_avg_centi", "gps_max_centi"};
        boolean any = false; for (String key : keys) any |= record.has(key);
        if (!any) return RideGps.NONE;
        long[] values = new long[4];
        for (int i = 0; i < keys.length; i++) {
            Object value = record.opt(keys[i]);
            if (!(value instanceof Number)) throw new IllegalArgumentException("骑行 GPS 字段不完整或格式异常");
            double number = ((Number)value).doubleValue();
            if (!Double.isFinite(number) || number < 0 || number > 4294967295.0 || number != Math.rint(number))
                throw new IllegalArgumentException("骑行 GPS 数值异常");
            values[i] = ((Number)value).longValue();
        }
        return new RideGps(values[0], values[1], values[2], values[3]);
    }
    private static String rideGpsText(RideGps gps) {
        if (!gps.available()) return "GPS  —（此记录没有有效定位统计）";
        return String.format(Locale.ROOT, "GPS 距离  %.3f km\n平均  %.2f km/h · 最高  %.2f km/h\n有效采样  %.1f 秒（均速仅按此时段计算）",
                gps.distanceM / 1000.0, gps.avgCenti / 100.0, gps.maxCenti / 100.0, gps.timeMs / 1000.0);
    }
    private static String syncDate(long millis) {
        return millis == 0 ? "未知（旧版缓存）" : DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(new Date(millis));
    }
    private static String deviceLabel(String device) {
        return "legacy-unknown".equals(device) ? "旧版记录 · 手表未知" : "手表 " + device;
    }
    private void showHistorySummary() {
        if (historySummary == null) return;
        List<RideHistory.Entry> records = history == null ? Collections.emptyList() : history.snapshot();
        long seconds = 0; for (RideHistory.Entry entry : records) seconds += entry.elapsed;
        historySummary.setText(history == null ? historyFailure : records.isEmpty() ? "还没有保存的骑行"
                : "已保存 " + records.size() + " 次骑行\n累计时长  " + time(seconds) + historyWarning);
        for (Button action : new Button[]{browseHistory, exportHistory}) {
            action.setEnabled(!records.isEmpty()); action.setAlpha(records.isEmpty() ? .38f : 1f);
        }
    }
    private void browseHistory() {
        if (history == null) return;
        List<RideHistory.Entry> records = history.snapshot();
        String[] titles = new String[records.size()];
        for (int i = 0; i < titles.length; i++) {
            RideHistory.Entry entry = records.get(i);
            titles[i] = "第 " + entry.sequence + " 次 · " + time(entry.elapsed) + "\n同步于 "
                    + syncDate(entry.firstSync) + "\n" + deviceLabel(entry.device);
        }
        new AlertDialog.Builder(this).setTitle("骑行历史 · " + records.size() + " 条")
                .setItems(titles, (dialog, index) -> showHistoryDetail(records.get(index)))
                .setNegativeButton("关闭", null).show();
    }
    private String historyDetail(RideHistory.Entry entry) {
        return deviceLabel(entry.device) + "\n第 " + entry.sequence + " 次记录\n\n总时长  " + time(entry.elapsed)
                + "\n运动时间  " + time(entry.moving) + "\n静止时间  " + time(entry.still)
                + "\n暂停  " + entry.pauses + " 次\n休息提醒  " + entry.rests + " 次\n冲击提醒  " + entry.impacts
                + " 次\n\n首次同步  " + syncDate(entry.firstSync) + "\n最近同步  " + syncDate(entry.lastSync)
                + "\n同步时间不代表骑行发生时间。"
                + "\n\n" + rideGpsText(entry.gps)
                + "\n\n" + entry.safety.summary()
                + (entry.note.isEmpty() ? "" : "\n\n已回传的备注\n" + entry.note);
    }
    private void showHistoryDetail(RideHistory.Entry entry) {
        TextView detail = text(historyDetail(entry), 16, INK); detail.setPadding(dp(22), dp(12), dp(22), dp(12));
        detail.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this); scroll.addView(detail);
        new AlertDialog.Builder(this).setTitle("骑行详情").setView(scroll)
                .setPositiveButton("分享摘要", (dialog, which) -> {
                    Intent share = new Intent(Intent.ACTION_SEND).setType("text/plain")
                            .putExtra(Intent.EXTRA_TEXT, "VelaRide 骑行记录\n" + historyDetail(entry));
                    try { startActivity(Intent.createChooser(share, "分享骑行摘要")); }
                    catch (ActivityNotFoundException error) { message.setText("当前手机没有可接收分享的应用。"); }
                }).setNegativeButton("关闭", null).show();
    }
    private void chooseExport() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("text/csv").putExtra(Intent.EXTRA_TITLE, "VelaRide-rides.csv");
        try { startActivityForResult(intent, EXPORT_HISTORY); }
        catch (ActivityNotFoundException error) { message.setText("当前手机没有文件保存工具，可先分享单条摘要。"); }
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != EXPORT_HISTORY || result != RESULT_OK || data == null || data.getData() == null || history == null) return;
        android.net.Uri destination = data.getData();
        List<RideHistory.Entry> snapshot = history.snapshot();
        message.setText("正在导出 " + snapshot.size() + " 条骑行记录…");
        worker.execute(() -> {
            String status;
            try (OutputStream output = getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) throw new IOException("无法打开目标文件");
                output.write(RideHistory.csv(snapshot).getBytes(StandardCharsets.UTF_8));
                output.flush();
                status = "已导出 " + snapshot.size() + " 条记录到所选文件。";
            } catch (Exception error) { status = "导出未完成，请重新选择文件重试：" + error.getMessage(); }
            final String display = status;
            main.post(() -> { if (!isDestroyed()) message.setText(display); });
        });
    }
    private void sendNote() {
        if (latest == null) return;
        String value = note.getText().toString().trim();
        if (value.isEmpty() || value.getBytes(StandardCharsets.UTF_8).length > 260) {
            message.setText("请输入简短备注，最多约 86 个汉字。"); return;
        }
        long sequence = latest.optLong("sequence");
        RideHistory.Entry entry = historyEntry(latest, store.getString("watch_address", "legacy-unknown"),
                store.getLong("synced_at", 0), value);
        runJob("正在回传备注…", epoch -> {
            ask(epoch, json("ride_review").put("sequence", sequence).put("text", value), "ride_review");
            if (history == null) throw new IOException("备注已到手表，但手机历史不可用。");
            try { history.upsert(entry); }
            catch (IOException error) { throw new IOException("备注已到手表，但手机留存失败：" + error.getMessage(), error); }
            main.post(() -> { if (epoch == session) note.setText(""); });
        });
    }

    private boolean bleAllowed() {
        if (Build.VERSION.SDK_INT >= 31) return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }
    private void connectWatch(boolean forceScan) {
        if (bluetooth == null) { message.setText("这台手机不支持蓝牙低功耗连接。"); return; }
        if (!bleAllowed()) {
            requestPermissions(Build.VERSION.SDK_INT >= 31 ?
                    new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT} :
                    new String[]{Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION}, BLE_PERMISSION);
            return;
        }
        if (!bluetooth.isEnabled()) { startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)); return; }
        if (Build.VERSION.SDK_INT < 31 && locationManager != null && !locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
                && !locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
            new AlertDialog.Builder(this).setMessage("此安卓版本需要开启系统定位，才能搜索附近蓝牙设备。")
                    .setPositiveButton("去开启", (d,w) -> startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS)))
                    .setNegativeButton("稍后", null).show(); return;
        }
        String address = store.getString("watch_address", "");
        if (!forceScan && BluetoothAdapter.checkBluetoothAddress(address)) {
            link.connect(bluetooth.getRemoteDevice(address)); return;
        }
        link.disconnect("正在搜索附近手表…");
        stopScan(); devices.clear();
        deviceNames = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1);
        scanDialog = new AlertDialog.Builder(this).setTitle("搜索 VelaRide…")
                .setAdapter(deviceNames, (dialog, which) -> {
                    BluetoothDevice selected = devices.get(which); stopScan();
                    if (!selected.getAddress().equals(store.getString("watch_address", ""))) {
                        latest = null; store.edit().remove("latest_record").remove("synced_at").apply();
                        cachedLabel.setText("还没有同步这块手表的骑行记录"); duration.setText("—");
                        moving.setText("运动时间  —"); recordDetail.setText("暂停  —    休息提醒  —");
                    }
                    store.edit().putString("watch_address", selected.getAddress()).apply(); link.connect(selected);
                }).setNegativeButton("取消", (d,w) -> stopScan()).create();
        scanDialog.setOnCancelListener(d -> stopScan()); scanDialog.show();
        scanner = bluetooth.getBluetoothLeScanner();
        if (scanner == null) { stopScan(); message.setText("蓝牙尚未就绪，请稍后重试。"); return; }
        scanning = true;
        final long thisScan = ++scanEpoch;
        try {
            scanner.startScan(Collections.singletonList(new ScanFilter.Builder()
                            .setServiceUuid(ParcelUuid.fromString("0000fff0-0000-1000-8000-00805f9b34fb")).build()),
                    new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback);
        } catch (RuntimeException error) { stopScan(); message.setText("无法开始搜索，请检查蓝牙权限。"); }
        main.postDelayed(() -> {
            if (scanning && thisScan == scanEpoch) {
                stopScan();
                if (scanDialog != null && scanDialog.isShowing()) scanDialog.setTitle(devices.isEmpty() ? "未找到手表，请确认已开机并靠近" : "选择你的手表");
            }
        }, 15000);
    }
    private void stopScan() {
        scanEpoch++;
        scanning = false;
        if (scanner != null) { try { scanner.stopScan(scanCallback); } catch (RuntimeException ignored) { } }
        scanner = null;
    }
    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult result) {
            main.post(() -> {
                if (!scanning || devices.contains(result.getDevice())) return;
                String name = result.getScanRecord() == null ? null : result.getScanRecord().getDeviceName();
                if (name != null && !"VelaRide".equals(name)) return;
                devices.add(result.getDevice()); String address = result.getDevice().getAddress();
                deviceNames.add("VelaRide  ·  " + address.substring(Math.max(0, address.length() - 5)));
            });
        }
        @Override public void onScanFailed(int code) {
            main.post(() -> { stopScan(); if (scanDialog != null) scanDialog.setTitle("搜索失败，请稍后重试"); });
        }
    };

    private void startGps() {
        if (!link.isReady() || !gpsSupported) return;
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION}, GPS_PERMISSION); return;
        }
        if (locationManager == null || !locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            message.setText("请先开启系统定位，建议在室外使用。"); startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS)); return;
        }
        try {
            gpsGeneration++; gpsSending = false; gpsStale = false; lastGpsFix = 0;
            gpsTracker = new GpsTracker();
            phoneDistance = 0; lastLocation = null; lastGpsSent = 0; gpsRunning = true;
            locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, locationListener, Looper.getMainLooper());
            gpsValue.setText("— km/h   ·   0.00 km");
            gps.setText("停止定位同步"); gpsStatus.setText("正在等待卫星定位，请靠近室外开阔处…");
            main.removeCallbacks(gpsFreshness); main.postDelayed(gpsFreshness, 1000);
        } catch (RuntimeException error) { gpsRunning = false; gpsStatus.setText("定位启动失败，请检查权限与系统设置。"); }
    }
    private void stopGps(boolean clearWatch) {
        boolean wasRunning = gpsRunning; gpsRunning = false; gpsGeneration++; gpsSending = false;
        main.removeCallbacks(gpsFreshness);
        if (locationManager != null) { try { locationManager.removeUpdates(locationListener); } catch (RuntimeException ignored) { } }
        if (gps != null) gps.setText("开始定位同步");
        if (wasRunning && gpsStatus != null) gpsStatus.setText("定位已停止 · 本次累计距离保留在手机显示");
        if (wasRunning && clearWatch && link.isReady()) {
            long epoch = session;
            worker.execute(() -> { try { ask(epoch, json("phone_clear"), "phone_clear"); } catch (Exception ignored) { } });
        }
    }
    private final Runnable gpsFreshness = new Runnable() {
        @Override public void run() {
            if (!gpsRunning) return;
            if (lastGpsFix != 0 && !gpsStale && SystemClock.elapsedRealtimeNanos() - lastGpsFix >= GpsTracker.MAX_AGE_NS) {
                gpsStale = true;
                gpsValue.setText(String.format(Locale.ROOT, "— km/h   ·   %.2f km", phoneDistance));
                gpsStatus.setText("定位信号已过期，正在重新定位；距离暂停累计。");
                final long epoch = session, run = gpsGeneration;
                worker.execute(() -> {
                    try {
                        if (gpsRunning && run == gpsGeneration && gpsStale)
                            ask(epoch, json("phone_clear"), "phone_clear");
                    } catch (Exception ignored) { /* Watch also expires data after 15 seconds. */ }
                });
            }
            main.postDelayed(this, 1000);
        }
    };
    private final LocationListener locationListener = new LocationListener() {
        @Override public void onLocationChanged(Location location) {
            if (!gpsRunning || !link.isReady() || !location.hasAccuracy()) return;
            double delta = lastLocation == null ? 0 : location.distanceTo(lastLocation);
            GpsTracker.Sample sample = gpsTracker.accept(SystemClock.elapsedRealtimeNanos(), location.getElapsedRealtimeNanos(),
                    location.getAccuracy(), delta, location.hasSpeed(), location.getSpeed());
            if (sample == null) return;
            final double speed = sample.speed;
            phoneDistance = sample.distance; lastGpsFix = location.getElapsedRealtimeNanos(); gpsStale = false;
            lastLocation = new Location(location);
            gpsValue.setText(String.format(Locale.ROOT, "%.1f km/h   ·   %.2f km", speed, phoneDistance));
            gpsStatus.setText("定位精度约 " + Math.round(location.getAccuracy()) + " 米 · 前台同步中");
            long now = SystemClock.elapsedRealtime();
            if (gpsSending || busy || now - lastGpsSent < 5000) return;
            gpsSending = true; lastGpsSent = now; final double distance = phoneDistance; final long epoch = session;
            final long run = gpsGeneration, fix = lastGpsFix;
            worker.execute(() -> {
                String failure = null;
                try {
                    if (gpsRunning && run == gpsGeneration && SystemClock.elapsedRealtimeNanos() - fix < GpsTracker.MAX_AGE_NS)
                        ask(epoch, json("phone_data").put("speed", speed).put("distance", distance), "phone_data");
                }
                catch (Exception error) { failure = readable(error); }
                final String warning = failure;
                main.post(() -> {
                    if (run != gpsGeneration) return;
                    gpsSending = false; if (warning != null && epoch == session) gpsStatus.setText(warning);
                });
            });
        }
        @Override public void onProviderDisabled(String provider) { stopGps(true); }
        @Override public void onProviderEnabled(String provider) { }
        @Override public void onStatusChanged(String provider, int status, Bundle extras) { }
    };
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code == BLE_PERMISSION) {
            if (bleAllowed()) connectWatch(true);
            else message.setText(Build.VERSION.SDK_INT >= 31 ? "需要附近设备权限，才能连接你的手表。" : "此安卓版本需要位置权限，才能搜索附近手表。");
        }
        if (code == GPS_PERMISSION) {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) startGps();
            else gpsStatus.setText("未开启精确位置权限，蓝牙与骑行同步仍可使用。");
        }
    }
    @Override protected void onStop() {
        stopScan();
        if (scanDialog != null && scanDialog.isShowing()) scanDialog.dismiss();
        stopGps(true); super.onStop();
    }
    @Override protected void onDestroy() {
        session++; stopScan(); stopGps(false); link.disconnect("连接已结束"); worker.shutdownNow(); super.onDestroy();
    }
    @Override protected void onSaveInstanceState(Bundle saved) {
        saved.putString("draft", note.getText().toString()); super.onSaveInstanceState(saved);
    }
}
