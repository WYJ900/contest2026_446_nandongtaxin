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
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.text.DateFormat;
import java.util.*;
import java.util.concurrent.*;

/** Direct watch companion; connectedDevice service owns BLE, GPS remains foreground-only. */
@SuppressLint("MissingPermission")
public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(16, 21, 18), CARD = Color.rgb(28, 36, 30);
    private static final int INK = Color.rgb(238, 245, 233), MUTED = Color.rgb(155, 173, 157);
    private static final int GREEN = Color.rgb(183, 237, 114), BLE_PERMISSION = 10, GPS_PERMISSION = 11;
    private final Handler main = new Handler(Looper.getMainLooper());
    private ExecutorService worker;
    private final ExecutorService ioWorker=Executors.newSingleThreadExecutor();
    private PhoneLinkSession phoneSession;
    private final BleLink.Listener connectionListener=this::connectionChanged;
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
    private Button reviewEntry;
    private String historyWarning = "";
    private static final int IMPORT_ROUTE = 13;
    private RoutePlan route;
    private TextView routeStatus;
    private volatile long routeGeneration;
    private volatile boolean routeSupported = true;
    private TextView voiceStatus;
    private String lastVoiceName="",lastVoiceDevice="";
    private android.media.MediaPlayer voicePlayer;
    private String historyFailure;
    private TextView bridgeStatus,weatherStatus;
    private static final int CONNECTION_NOTIFICATION_PERMISSION=14, EMERGENCY_PERMISSION=15;
    private EmergencyCenter emergency;
    private TextView emergencyStatus;
    private final EmergencyCenter.Listener emergencyListener=()->main.post(this::renderEmergency);

    @Override public void onCreate(Bundle stateBundle) {
        super.onCreate(stateBundle);
        store = getSharedPreferences("velaride", MODE_PRIVATE);
        BluetoothManager manager = getSystemService(BluetoothManager.class);
        bluetooth = manager == null ? null : manager.getAdapter();
        locationManager = getSystemService(LocationManager.class);
        phoneSession=PhoneLinkSession.get(this);link=phoneSession.link;worker=phoneSession.worker;
        emergency=EmergencyCenter.get(this);
        try {
            if (sharedHistory == null) sharedHistory = new RideHistory(new HistoryFile(getFilesDir()));
            history = sharedHistory;
        }
        catch (IOException error) { historyFailure = "历史暂时无法读取，原文件已保留：" + error.getMessage(); }
        makeScreen();
        phoneSession.add(connectionListener);
        emergency.add(emergencyListener);renderEmergency();
        if(link.isReady())connectionChanged(true,"手表后台连接已恢复到界面");
        lastVoiceName=store.getString("voice_file","");lastVoiceDevice=store.getString("voice_watch","");
        if(!lastVoiceName.isEmpty()) voiceStatus.setText("手机已保存 "+lastVoiceName+"，点击试听或选择关联记录。");
        worker.execute(() -> {
            File saved = new File(getFilesDir(), "route.gpx");
            if (!saved.exists()) return;
            try (InputStream input = new android.util.AtomicFile(saved).openRead()) {
                RoutePlan loaded = RoutePlan.parse(readRouteBytes(input));
                main.post(() -> { if (!isDestroyed()) { route=loaded; routeGeneration++;
                    routeStatus.setText("已恢复 GPX · " + loaded.size() + " 个路线点，开启定位后指引。"); } });
            } catch (Exception error) { main.post(() -> { if (!isDestroyed()) routeStatus.setText("保存的路线暂不可读，请重新导入。"); }); }
        });
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
                                && (!migrated.safety.available() || saved.safety.available())
                                && (!migrated.extras.available() || saved.extras.available()); break;
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
            if (link.isReady()) phoneSession.disconnect("已主动断开手表"); else connectWatch(false);
        });
        search = button(device, "搜索附近的 VelaRide", false, v -> connectWatch(true));
        refresh = button(device, "刷新手表状态", false, v -> runJob("正在读取手表…", epoch -> readStatus(epoch)));
        clock = button(device, "同步手机时间", false, v -> runJob("正在校时…", epoch -> syncTime(epoch)));

        LinearLayout sos=card(root,"摔倒呼救","手表检测到撞击后静止，30 秒内未在腕上确认，或你在手表上点“需要帮助”，手机会响铃倒计时，随后给紧急联系人发送定位短信并拨打第一位联系人。");
        emergencyStatus=text("",13,MUTED);sos.addView(emergencyStatus);
        CheckBox sosEnabled=new CheckBox(this);sosEnabled.setText("开启摔倒自动呼救");sosEnabled.setTextColor(INK);
        sosEnabled.setChecked(emergency.enabled());sos.addView(sosEnabled);
        sosEnabled.setOnCheckedChangeListener((view,checked)->{
            emergency.prefs().edit().putBoolean("enabled",checked).apply();
            if(checked)requestEmergencyPermissions();
            renderEmergency();
        });
        EditText contacts=new EditText(this);contacts.setTextColor(INK);contacts.setHintTextColor(MUTED);contacts.setTextSize(15);
        contacts.setHint("每行一位，最多 3 位，例如：\n妈妈 13800000000");contacts.setMinLines(2);contacts.setMaxLines(4);
        contacts.setText(emergency.prefs().getString("contacts",""));
        sos.addView(contacts,new LinearLayout.LayoutParams(-1,-2));
        button(sos,"保存紧急联系人",true,v->{
            try{
                List<EmergencyPlan.Contact> parsed=EmergencyPlan.parse(contacts.getText().toString());
                String saved=EmergencyPlan.format(parsed);
                emergency.prefs().edit().putString("contacts",saved).apply();contacts.setText(saved);
                message.setText(parsed.isEmpty()?"已清空紧急联系人。":"已保存 "+parsed.size()+" 位紧急联系人。");
            }catch(IllegalArgumentException error){message.setText(error.getMessage());}
            renderEmergency();
        });
        button(sos,"授权短信、电话、定位和提醒",false,v->requestEmergencyPermissions());
        button(sos,"允许锁屏全屏提醒",false,v->{
            try{
                if(Build.VERSION.SDK_INT>=34)startActivity(new Intent(Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT,
                        android.net.Uri.parse("package:"+getPackageName())));
                else startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,android.net.Uri.parse("package:"+getPackageName())));
            }catch(ActivityNotFoundException error){message.setText("请在系统设置中允许 VelaRide 的锁屏显示和悬浮通知。");}
        });
        button(sos,"呼救演练（不发短信、不拨号）",false,v->emergency.arm(FallAlert.Reason.DRILL,true));
        button(sos,"给联系人发送测试短信",false,v->new AlertDialog.Builder(this).setTitle("发送测试短信？")
                .setMessage("将向已保存的紧急联系人发送一条标注“演练测试”的定位短信，不会拨号。")
                .setPositiveButton("发送",(d,w)->emergency.sendTest()).setNegativeButton("取消",null).show());

        LinearLayout bridge=card(root,"红米消息与天气","蓝牙连接由前台服务保持；消息需你授权并选择应用，默认只提示有新消息。");
        bridgeStatus=text("尚未授权手机消息",13,MUTED);bridge.addView(bridgeStatus);
        CheckBox relay=new CheckBox(this);relay.setText("转发已选应用的通知");relay.setTextColor(INK);
        relay.setChecked(bridgePrefs().getBoolean("relay_enabled",false));bridge.addView(relay);
        relay.setOnCheckedChangeListener((view,checked)->{bridgePrefs().edit().putBoolean("relay_enabled",checked).apply();renderBridgeStatus();});
        CheckBox preview=new CheckBox(this);preview.setText("在手表显示消息正文摘要");preview.setTextColor(INK);
        preview.setChecked(bridgePrefs().getBoolean("previews",false));bridge.addView(preview);
        preview.setOnCheckedChangeListener((view,checked)->{bridgePrefs().edit().putBoolean("previews",checked).apply();renderBridgeStatus();});
        button(bridge,"选择要转发的应用",false,v->chooseNotificationApps());
        button(bridge,"授权系统通知读取",false,v->{
            try{startActivity(new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS));}
            catch(ActivityNotFoundException error){message.setText("请在系统设置搜索通知使用权并选择 VelaRide。");}
        });
        button(bridge,"允许后台连接状态通知",false,v->{
            if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},CONNECTION_NOTIFICATION_PERMISSION);
            else message.setText("后台连接状态通知已具备权限。");
        });
        button(bridge,"红米后台/省电设置",false,v->startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                android.net.Uri.parse("package:"+getPackageName()))));
        weatherStatus=text("系统天气尚未同步；部分 ROM 可能不向普通 App 开放读取。",13,MUTED);gap(bridge,10);bridge.addView(weatherStatus);
        button(bridge,"尝试同步小米系统天气",false,v->syncSystemWeather());
        button(bridge,"打开小米天气",false,v->openPhoneApp("com.miui.weather2"));
        button(bridge,"打开小米运动健康",false,v->openHealthApp());

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
        reviewEntry = button(archive, "最近骑行复盘与建议", false, v -> {
            if (history != null && !history.snapshot().isEmpty()) showReview(history.snapshot().get(0));
        });
        exportHistory = button(archive, "导出全部记录 CSV", false, v -> chooseExport());

        LinearLayout position = card(root, "手机定位同步", "开启后将手机速度与本次累计距离发给手表。仅在 App 前台使用。");
        gpsValue = text("0.0 km/h   ·   0.00 km", 23, INK); position.addView(gpsValue);
        gpsStatus = text("尚未开启 · 需要手机位置权限", 13, MUTED); gap(position, 8); position.addView(gpsStatus);
        gps = button(position, "开始定位同步", false, v -> { if (gpsRunning) stopGps(true); else startGps(); });

        LinearLayout navigation = card(root, "GPX 路线点指引", "按导入路线显示下一点的方向和直线距离；没有道路重规划。需保持前台定位。");
        routeStatus = text("尚未导入路线", 14, MUTED); navigation.addView(routeStatus);
        button(navigation, "导入 GPX 路线", false, v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            try { startActivityForResult(intent, IMPORT_ROUTE); }
            catch (ActivityNotFoundException error) { message.setText("当前手机没有文件选择工具。"); }
        });
        button(navigation, "停止路线指引", false, v -> {
            route=null; routeGeneration++;
            new android.util.AtomicFile(new File(getFilesDir(),"route.gpx")).delete();
            routeStatus.setText("路线指引已停止，定位同步可继续。");
            if(link.isReady()) runJob("正在清除腕上路线…", epoch -> {
                try { ask(epoch,json("nav_clear"),"nav_clear"); }
                catch(BleLink.DeviceError error) { if(!"unknown_cmd".equals(error.getMessage())) throw error; }
            });
        });

        LinearLayout voice=card(root,"停车语音标记","在手表暂停后录制 3 秒，保存到 TF 卡。这里可校验同步、试听并选择关联骑行。");
        voiceStatus=text("尚未同步语音；需要手表已挂载 FAT TF 卡。",13,MUTED);voice.addView(voiceStatus);
        button(voice,"同步手表最近录音",false,v->downloadVoice());
        button(voice,"试听已同步录音",false,v->{try{playVoice(voiceFile(lastVoiceName));}catch(Exception error){message.setText(readable(error));}});
        button(voice,"将录音关联到骑行",false,v->bindVoice());

        LinearLayout memo = card(root, "骑行备注", "把一句简短备注保存到手表的最近一次骑行中。");
        note = new EditText(this); note.setTextColor(INK); note.setHintTextColor(MUTED);
        note.setTextSize(15); note.setHint("例如：今天骑得很舒服，下次继续"); note.setMinLines(2); note.setMaxLines(4);
        memo.addView(note, new LinearLayout.LayoutParams(-1, -2));
        saveNote = button(memo, "保存备注到手表", false, v -> sendNote());
        message = text("记录保存在本机；主动请求在线复盘时，摘要会发送到你配置的服务。", 12, MUTED); root.addView(message);
    }

    private void requestEmergencyPermissions() {
        List<String> wanted=new ArrayList<>(Arrays.asList(Manifest.permission.SEND_SMS,Manifest.permission.CALL_PHONE,
                Manifest.permission.ACCESS_FINE_LOCATION,Manifest.permission.ACCESS_COARSE_LOCATION));
        if(Build.VERSION.SDK_INT>=33)wanted.add(Manifest.permission.POST_NOTIFICATIONS);
        List<String> missing=new ArrayList<>();
        for(String p:wanted)if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED)missing.add(p);
        if(missing.isEmpty())message.setText("摔倒呼救所需权限已具备。红米手机请在应用权限中把“发送短信”设为始终允许。");
        else requestPermissions(missing.toArray(new String[0]),EMERGENCY_PERMISSION);
    }
    private boolean granted(String permission){return checkSelfPermission(permission)==PackageManager.PERMISSION_GRANTED;}
    private void renderEmergency() {
        if(emergencyStatus==null||emergency==null)return;
        int people=emergency.contacts().size();
        StringBuilder text=new StringBuilder();
        text.append(emergency.enabled()?"已开启":"未开启").append(" · 紧急联系人 ").append(people).append(" 位\n")
            .append("短信").append(granted(Manifest.permission.SEND_SMS)?"✓":"✗").append("  电话").append(granted(Manifest.permission.CALL_PHONE)?"✓":"✗")
            .append("  定位").append(granted(Manifest.permission.ACCESS_FINE_LOCATION)?"✓":"✗");
        if(Build.VERSION.SDK_INT>=33)text.append("  通知").append(granted(Manifest.permission.POST_NOTIFICATIONS)?"✓":"✗");
        if(Build.VERSION.SDK_INT>=34){
            NotificationManager manager=getSystemService(NotificationManager.class);
            text.append("  锁屏全屏").append(manager!=null&&manager.canUseFullScreenIntent()?"✓":"✗");
        }
        text.append("\n需要保持手表连接（后台服务会持续读取安全状态）。");
        if(emergency.enabled()&&people==0)text.append("\n⚠ 还没有紧急联系人，触发时无法自动求助。");
        if(!emergency.watchNote().isEmpty())text.append("\n").append(emergency.watchNote());
        if(!emergency.counting()&&!"未触发".equals(emergency.status()))text.append("\n上次结果：").append(emergency.status());
        emergencyStatus.setText(text.toString());
        emergencyStatus.setTextColor(emergency.enabled()&&people>0?GREEN:MUTED);
    }
    private SharedPreferences bridgePrefs(){return getSharedPreferences("phone-bridge",MODE_PRIVATE);}
    private boolean notificationAccess() {
        try {
            String enabled=Settings.Secure.getString(getContentResolver(),"enabled_notification_listeners");
            ComponentName mine=new ComponentName(this,PhoneNotificationService.class);
            if(enabled!=null)for(String item:enabled.split(":"))if(mine.equals(ComponentName.unflattenFromString(item)))return true;
        } catch(SecurityException ignored){}
        return false;
    }
    private void renderBridgeStatus() {
        if(bridgeStatus==null)return;
        SharedPreferences settings=bridgePrefs();
        bridgeStatus.setText("通知读取 "+(notificationAccess()?"已授权":"未授权")+" · 已选 "+
                settings.getStringSet("selected_apps",Collections.emptySet()).size()+" 个应用\n"
                +(settings.getBoolean("relay_enabled",false)?"转发已开启":"转发已关闭")+" · "
                +(settings.getBoolean("previews",false)?"显示正文摘要":"仅提示有新消息")+"\n"
                +(link!=null&&link.isReady()?settings.getString("relay_status","蓝牙已连接，等待新消息"):"请连接手表以转发新消息"));
    }
    private void chooseNotificationApps() {
        Intent launcher=new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        TreeMap<String,String> apps=new TreeMap<>();
        for(android.content.pm.ResolveInfo resolved:getPackageManager().queryIntentActivities(launcher,0)) {
            String pkg=resolved.activityInfo.packageName;
            if(!getPackageName().equals(pkg))apps.put(pkg,resolved.loadLabel(getPackageManager()).toString());
        }
        if(apps.isEmpty()){message.setText("未能读取可选应用，请检查系统应用可见性设置。");return;}
        List<String> packages=new ArrayList<>(apps.keySet());
        packages.sort((a,b)->apps.get(a).compareToIgnoreCase(apps.get(b)));
        Set<String> selected=new HashSet<>(bridgePrefs().getStringSet("selected_apps",Collections.emptySet()));
        String[] labels=new String[packages.size()];boolean[] checks=new boolean[packages.size()];
        for(int i=0;i<labels.length;i++){labels[i]=apps.get(packages.get(i));checks[i]=selected.contains(packages.get(i));}
        new AlertDialog.Builder(this).setTitle("选择转发通知的应用")
                .setMultiChoiceItems(labels,checks,(dialog,index,checked)->{if(checked)selected.add(packages.get(index));else selected.remove(packages.get(index));})
                .setPositiveButton("保存",(dialog,which)->{bridgePrefs().edit().putStringSet("selected_apps",selected).apply();renderBridgeStatus();})
                .setNegativeButton("取消",null).show();
    }
    private void openPhoneApp(String pkg) {
        Intent intent=getPackageManager().getLaunchIntentForPackage(pkg);
        if(intent==null){message.setText("此手机未提供可打开的对应应用。");return;}
        try{startActivity(intent);}catch(ActivityNotFoundException error){message.setText("系统无法打开对应应用。");}
    }
    private void openHealthApp() {
        for(String pkg:new String[]{"com.mi.health","com.xiaomi.wearable"})if(getPackageManager().getLaunchIntentForPackage(pkg)!=null){openPhoneApp(pkg);return;}
        message.setText("未找到小米运动健康；骑行记录仍可在本 App 查看并导出 CSV。");
    }
    private void syncSystemWeather() {
        if(busy)return;
        if(!link.isReady()){weatherStatus.setText("请先连接手表，再同步系统天气。");return;}
        weatherStatus.setText("正在读取系统天气…");
        runJobOn(ioWorker,"正在同步系统天气…",epoch->{
            try {
                WeatherData data=MiuiWeatherSource.read(this);
                JSONObject command=json("phone_weather").put("city",data.city).put("description",data.description)
                        .put("temperature_deci",data.temperatureDeci).put("published_epoch",data.publishedEpoch);
                Future<JSONObject> request=worker.submit(()->ask(epoch,command,"phone_weather"));
                try {request.get(20,TimeUnit.SECONDS);}
                catch(ExecutionException failed){Throwable cause=failed.getCause();if(cause instanceof Exception)throw (Exception)cause;throw failed;}
                catch(TimeoutException timedOut){request.cancel(true);throw new IOException("天气同步超时，请重新连接后重试。",timedOut);}
                main.post(()->{if(epoch==session)weatherStatus.setText(String.format(Locale.ROOT,"已同步 %s · %.1f C · %s",data.city,data.temperatureDeci/10.0,data.description));});
            } catch(Exception error) {
                final String text=error instanceof BleLink.DeviceError&&"unknown_cmd".equals(error.getMessage())?"手表需升级后显示手机天气。":readable(error);
                main.post(()->{if(epoch==session)weatherStatus.setText(text);});throw error;
            }
        });
    }
    @Override protected void onResume(){super.onResume();renderBridgeStatus();}
    private void connectionChanged(boolean ready, String reason) {
        if (!ready) { session++; busy = false; gpsSupported = false; stopGps(false); }
        else routeSupported = true;
        state.setText(reason); state.setTextColor(ready ? GREEN : MUTED);
        renderBridgeStatus();
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
        runJobOn(worker,title,job);
    }
    private void runJobOn(ExecutorService executor,String title,Job job) {
        if (busy) return;
        long epoch = session; busy = true; message.setText(title); updateButtons();
        executor.execute(() -> {
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
            summary += emergency.enabled() ? "\n手表已选择“需要帮助”，手机已按摔倒呼救设置处理。"
                    : "\n手表已选择“需要帮助”；摔倒呼救未开启，请手动联系他人。";
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
                + rideGpsText(parseRideGps(record)) + "\n\n" + parseRideSafety(record).summary()
                + "\n\n" + parseRideExtras(record).summary());
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
        RideExtras extras = parseRideExtras(record);
        if (extras.restTaken+extras.restSnoozed+extras.restIgnored > record.getLong("rest_count"))
            throw new IOException("休息处理统计异常");
    }
    private static String time(long seconds) {
        return String.format(Locale.ROOT, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
    private RideHistory.Entry historyEntry(JSONObject record, String device, long syncedAt, String memo) {
        if (device.isEmpty()) device = "legacy-unknown";
        return new RideHistory.Entry(device, record.optLong("sequence"), record.optLong("elapsed_s"),
                record.optLong("moving_s"), record.optLong("still_s"), record.optLong("pause_count"),
                record.optLong("rest_count"), record.optLong("impacts"), syncedAt, syncedAt, memo,
                parseRideGps(record), parseRideSafety(record), parseRideExtras(record));
    }
    private RideExtras parseRideExtras(JSONObject record) {
        String[] keys={"extras_version","goal_distance_m","lap_count","rest_taken","rest_snoozed",
                "rest_ignored","drink_count","drink_done","drink_snoozed","drink_ignored"};
        boolean any=false; for(String key:keys) any |= record.has(key);
        if(!any) return RideExtras.NONE;
        long[] values=new long[keys.length];
        for(int i=0;i<keys.length;i++) {
            Object value=record.opt(keys[i]);
            if(!(value instanceof Number)) throw new IllegalArgumentException("骑行扩展字段不完整");
            double number=((Number)value).doubleValue();
            if(!Double.isFinite(number)||number<0||number>4294967295.0||number!=Math.rint(number))
                throw new IllegalArgumentException("骑行扩展数值异常");
            values[i]=((Number)value).longValue();
        }
        return new RideExtras(values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9]);
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
        for (Button action : new Button[]{browseHistory, exportHistory, reviewEntry}) {
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
                + "\n\n" + entry.extras.summary()
                + (entry.note.isEmpty() ? "" : "\n\n已回传的备注\n" + entry.note);
    }
    private void showHistoryDetail(RideHistory.Entry entry) {
        TextView detail = text(historyDetail(entry), 16, INK); detail.setPadding(dp(22), dp(12), dp(22), dp(12));
        detail.setTextIsSelectable(true);
        LinearLayout panel=new LinearLayout(this);panel.setOrientation(LinearLayout.VERTICAL);panel.addView(detail);
        String attached=store.getString("voice_bind_"+RideReview.recordIdentity(entry),"");
        if(!attached.isEmpty()) button(panel,"试听关联语音",false,v->{
            try{playVoice(voiceFile(attached));}catch(Exception error){message.setText(readable(error));}
        });
        ScrollView scroll = new ScrollView(this); scroll.addView(panel);
        new AlertDialog.Builder(this).setTitle("骑行详情").setView(scroll)
                .setNeutralButton("骑行复盘", (dialog, which) -> showReview(entry))
                .setPositiveButton("分享摘要", (dialog, which) -> {
                    Intent share = new Intent(Intent.ACTION_SEND).setType("text/plain")
                            .putExtra(Intent.EXTRA_TEXT, "VelaRide 骑行记录\n" + historyDetail(entry));
                    try { startActivity(Intent.createChooser(share, "分享骑行摘要")); }
                    catch (ActivityNotFoundException error) { message.setText("当前手机没有可接收分享的应用。"); }
                }).setNegativeButton("关闭", null).show();
    }
    private JSONObject cachedReview(RideHistory.Entry entry) {
        try {
            String value = getSharedPreferences("ride-reviews", MODE_PRIVATE).getString(RideReview.identity(entry), "");
            return value.isEmpty() ? null : new JSONObject(value);
        } catch (Exception error) { return null; }
    }
    private void showReview(RideHistory.Entry entry) {
        JSONObject saved = cachedReview(entry);
        LinearLayout panel = new LinearLayout(this); panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(20), dp(8), dp(20), dp(8));
        String online = saved == null ? "" : "\n\n已保存的" + ("fallback".equals(saved.optString("source"))
                ? "服务端规则复盘" : "在线 AI 复盘") + "\n" + saved.optString("text");
        TextView detail = text(RideReview.local(entry, history.snapshot()) + online, 15, INK);
        detail.setTextIsSelectable(true); panel.addView(detail);
        if (saved != null) {
            Button send = button(panel, "回传到手表最近记录", false,
                    v -> sendSavedReview(entry, saved.optString("watch")));
            send.setEnabled(link.isReady());
        }
        ScrollView scroll = new ScrollView(this); scroll.addView(panel);
        new AlertDialog.Builder(this).setTitle("记录 #" + entry.sequence + " · 骑行复盘").setView(scroll)
                .setPositiveButton("在线 AI 复盘", (dialog, which) -> onlineReview(entry))
                .setNeutralButton("服务地址", (dialog, which) -> configureReviewEndpoint(entry))
                .setNegativeButton("关闭", null).show();
    }
    private void configureReviewEndpoint(RideHistory.Entry entry) {
        EditText input = new EditText(this); input.setSingleLine(true);
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setText(store.getString("review_endpoint", ""));
        input.setHint("https://你的服务/api/ride/review");
        new AlertDialog.Builder(this).setTitle("复盘服务 HTTPS 地址")
                .setMessage("仅主动请求时发送这条骑行的统计摘要。AI 凭据在服务端配置，App 不保存模型 Key。")
                .setView(input).setPositiveButton("保存", (dialog, which) -> {
                    try {
                        String value = input.getText().toString().trim();
                        ReviewClient.endpoint(value);
                        store.edit().putString("review_endpoint", value).apply();
                        showReview(entry);
                    } catch (Exception error) { message.setText(readable(error)); }
                }).setNegativeButton("取消", null).show();
    }
    private JSONObject reviewPayload(RideHistory.Entry e) throws Exception {
        JSONObject payload = new JSONObject().put("sequence", e.sequence).put("elapsed_s", e.elapsed)
                .put("moving_s", e.moving).put("still_s", e.still).put("pause_count", e.pauses)
                .put("rest_count", e.rests).put("impacts", e.impacts);
        if (e.gps.available()) payload.put("distance_km", e.gps.distanceM / 1000.0)
                .put("gps_time_ms", e.gps.timeMs).put("gps_distance_m", e.gps.distanceM)
                .put("gps_avg_centi", e.gps.avgCenti).put("gps_max_centi", e.gps.maxCenti);
        if (e.safety.available()) payload.put("safety_version", 1).put("safety_checks", e.safety.checks)
                .put("safety_confirmed", e.safety.confirmed).put("safety_pauses", e.safety.pauses)
                .put("safety_help", e.safety.help).put("safety_unanswered", e.safety.unanswered)
                .put("safety_last_action", e.safety.lastAction).put("safety_last_elapsed_s", e.safety.lastElapsed);
        if(e.extras.available()) payload.put("extras_version",1).put("goal_distance_m",e.extras.goalM)
                .put("lap_count",e.extras.laps).put("rest_taken",e.extras.restTaken).put("rest_snoozed",e.extras.restSnoozed)
                .put("rest_ignored",e.extras.restIgnored).put("drink_count",e.extras.drinks).put("drink_done",e.extras.drinkDone)
                .put("drink_snoozed",e.extras.drinkSnoozed).put("drink_ignored",e.extras.drinkIgnored);
        return payload;
    }
    private void onlineReview(RideHistory.Entry entry) {
        String endpoint = store.getString("review_endpoint", "");
        if (endpoint.isEmpty()) { configureReviewEndpoint(entry); return; }
        runJobOn(ioWorker,"正在生成在线复盘…", epoch -> {
            JSONObject result = new JSONObject(new ReviewClient().post(endpoint, reviewPayload(entry).toString()));
            if (!result.optBoolean("ok") || result.optString("review").trim().isEmpty()
                    || result.optString("source").isEmpty()) throw new IOException("复盘服务回复不完整");
            String text = result.getString("review");
            if (text.length() > 4096) throw new IOException("复盘回复过长");
            String watchSource = result.optString("watch_source", result.getString("source"));
            String watch = RideReview.watchText("font-safe".equals(watchSource) ? "fallback" : watchSource,
                    result.optString("watch_review", text));
            JSONObject saved = new JSONObject().put("text", text).put("watch", watch)
                    .put("source", result.getString("source")).put("saved_at", System.currentTimeMillis());
            SharedPreferences cache = getSharedPreferences("ride-reviews", MODE_PRIVATE);
            SharedPreferences.Editor edit = cache.edit();
            String key = RideReview.identity(entry);
            if (!cache.contains(key) && cache.getAll().size() >= 128) {
                String oldest = null; long time = Long.MAX_VALUE;
                for (Map.Entry<String, ?> item : cache.getAll().entrySet()) {
                    long savedAt;
                    try { savedAt = new JSONObject(String.valueOf(item.getValue())).optLong("saved_at"); }
                    catch (Exception error) { savedAt = 0; }
                    if (savedAt < time) { oldest = item.getKey(); time = savedAt; }
                }
                if (oldest != null) edit.remove(oldest);
            }
            if (!edit.putString(key, saved.toString()).commit()) throw new IOException("复盘保存失败，请重试");
            main.post(() -> { if (!isDestroyed()) showReview(entry); });
        });
    }
    private void sendSavedReview(RideHistory.Entry entry, String watch) {
        if (!entry.device.equals(store.getString("watch_address", ""))) {
            message.setText("请连接这条记录所属的手表。"); return;
        }
        runJob("正在回传复盘…", epoch -> {
            JSONObject current = ask(epoch, json("ride_latest"), "ride_latest");
            validateRecord(current);
            RideHistory.Entry now = historyEntry(current, entry.device, System.currentTimeMillis(), "");
            if (!RideReview.identity(now).equals(RideReview.identity(entry)))
                throw new IOException("手表最新记录已变化，这条历史复盘仅保留在手机。");
            ask(epoch, json("ride_review").put("sequence", entry.sequence).put("text", watch), "ride_review");
        });
    }
    private void chooseExport() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("text/csv").putExtra(Intent.EXTRA_TITLE, "VelaRide-rides.csv");
        try { startActivityForResult(intent, EXPORT_HISTORY); }
        catch (ActivityNotFoundException error) { message.setText("当前手机没有文件保存工具，可先分享单条摘要。"); }
    }
    private File voiceFile(String name) throws IOException {
        if(!name.matches("[A-Za-z0-9_-]+\\.wav"))throw new IOException("尚无可试听的手机录音");
        File folder=new File(getFilesDir(),"voice");
        if(!folder.isDirectory()&&!folder.mkdirs())throw new IOException("语音目录不可用");
        return new File(folder,name);
    }
    private static long uintField(JSONObject json,String key) throws Exception {
        Object value=json.get(key);
        if(!(value instanceof Number))throw new IOException("语音字段类型异常");
        double number=((Number)value).doubleValue();
        if(!Double.isFinite(number)||number<0||number>4294967295.0||number!=Math.rint(number))throw new IOException("语音字段数值异常");
        return ((Number)value).longValue();
    }
    private void downloadVoice() {
        if(!link.isReady()){message.setText("请先连接手表。");return;}
        final String device=store.getString("watch_address","");
        runJob("正在同步录音；请保持手表暂停…",epoch->{
            JSONObject status=ask(epoch,json("ride_status"),"ride_status");
            if(status.optBoolean("active")&&!status.optBoolean("paused"))throw new IOException("请先暂停骑行，再同步语音。");
            JSONObject list;
            try{list=ask(epoch,json("memo_list"),"memo_list");}
            catch(BleLink.DeviceError error){if("unknown_cmd".equals(error.getMessage()))throw new IOException("当前手表需升级后才能同步录音。");throw error;}
            JSONArray ids=list.getJSONArray("ids");
            if(ids.length()>16)throw new IOException("手表录音清单过长");
            if(ids.length()==0)throw new IOException("TF 卡中没有已完成录音。");
            Object idValue=ids.get(0);
            if(!(idValue instanceof Number))throw new IOException("录音清单异常");
            double idNumber=((Number)idValue).doubleValue();
            if(!Double.isFinite(idNumber)||idNumber<1||idNumber>9999999||idNumber!=Math.rint(idNumber))throw new IOException("录音编号异常");
            long id=((Number)idValue).longValue();
            JSONObject details=ask(epoch,json("memo_info").put("id",id),"memo_info");
            if(!(details.get("pending") instanceof Boolean))throw new IOException("录音关联状态异常");
            VoiceMemo.Info info=new VoiceMemo.Info(uintField(details,"id"),uintField(details,"sequence"),uintField(details,"bytes"),
                    uintField(details,"crc32"),details.getBoolean("pending"));
            if(info.id!=id)throw new IOException("录音编号已变化");
            byte[] bytes=VoiceMemo.download(info,(offset,length)->{
                if(offset%1600==0) {
                    JSONObject now=ask(epoch,json("ride_status"),"ride_status");
                    if(now.optBoolean("active")&&!now.optBoolean("paused"))throw new IOException("骑行已继续，语音同步中止，请停车后重试。");
                    final long done=offset;
                    main.post(()->{if(epoch==session)voiceStatus.setText("同步中 "+done+" / "+info.bytes+" 字节，请保持暂停。");});
                }
                JSONObject chunk=ask(epoch,json("memo_read").put("id",id).put("offset",offset).put("length",length),"memo_chunk");
                if(uintField(chunk,"id")!=id||uintField(chunk,"offset")!=offset)throw new IOException("录音分片序号不一致");
                return VoiceMemo.hex(chunk.getString("data"),length);
            });
            String name=device.replaceAll("[^A-Za-z0-9]","")+"-M"+id+"-"+info.crc+".wav";
            android.util.AtomicFile file=new android.util.AtomicFile(voiceFile(name));
            FileOutputStream output=file.startWrite();
            try{output.write(bytes);output.getFD().sync();file.finishWrite(output);}
            catch(IOException error){file.failWrite(output);throw error;}
            if(!Arrays.equals(bytes,file.readFully()))throw new IOException("手机录音保存校验失败，请重试。");
            store.edit().putString("voice_file",name).putString("voice_watch",device).apply();
            main.post(()->{if(epoch==session){lastVoiceName=name;lastVoiceDevice=device;
                voiceStatus.setText("已校验保存 3 秒录音。录制时"+(info.pending?"会话尚未结束，编号提示 #":"骑行编号 #")
                        +info.sequence+"；请选择实际骑行关联。");}});
        });
    }
    private void bindVoice() {
        try{if(!voiceFile(lastVoiceName).isFile())throw new IOException("请先同步录音。");}
        catch(IOException error){message.setText(readable(error));return;}
        List<RideHistory.Entry> choices=new ArrayList<>();
        if(history!=null)for(RideHistory.Entry entry:history.snapshot())if(entry.device.equals(lastVoiceDevice))choices.add(entry);
        if(choices.isEmpty()){message.setText("请先同步这块手表的骑行记录，再选择关联。");return;}
        String[] labels=new String[choices.size()];
        for(int i=0;i<labels.length;i++)labels[i]="#"+choices.get(i).sequence+" · "+time(choices.get(i).elapsed);
        new AlertDialog.Builder(this).setTitle("选择这段录音对应的骑行").setItems(labels,(dialog,index)->{
            store.edit().putString("voice_bind_"+RideReview.recordIdentity(choices.get(index)),lastVoiceName).apply();
            voiceStatus.setText("已关联到手机记录 #"+choices.get(index).sequence+"，可在历史详情试听。");
        }).setNegativeButton("取消",null).show();
    }
    private void stopVoice() {
        if(voicePlayer!=null){voicePlayer.release();voicePlayer=null;}
    }
    private void playVoice(File file) throws IOException {
        if(!file.isFile())throw new IOException("语音文件不存在，请重新同步。");
        stopVoice();
        android.media.MediaPlayer player=new android.media.MediaPlayer();voicePlayer=player;
        try {
            player.setDataSource(file.getAbsolutePath());
            player.setOnPreparedListener(ready->{if(voicePlayer==ready)ready.start();});
            player.setOnCompletionListener(done->{if(voicePlayer==done)stopVoice();});
            player.setOnErrorListener((failed,what,extra)->{if(voicePlayer==failed)stopVoice();message.setText("当前录音无法播放，请重新同步。");return true;});
            player.prepareAsync();
        } catch(IOException|RuntimeException error) {
            if(voicePlayer==player)stopVoice();
            throw new IOException("当前录音无法播放，请重新同步。",error);
        }
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if(request==IMPORT_ROUTE && result==RESULT_OK && data!=null && data.getData()!=null) {
            final android.net.Uri source=data.getData();
            worker.execute(() -> {
                try(InputStream input=getContentResolver().openInputStream(source)) {
                    if(input==null) throw new IOException("无法打开路线文件");
                    byte[] bytes=readRouteBytes(input);
                    RoutePlan loaded=RoutePlan.parse(bytes);
                    android.util.AtomicFile file=new android.util.AtomicFile(new File(getFilesDir(),"route.gpx"));
                    FileOutputStream output=file.startWrite();
                    try { output.write(bytes);output.getFD().sync();file.finishWrite(output); }
                    catch(IOException error) { file.failWrite(output);throw error; }
                    if(!Arrays.equals(bytes,file.readFully()))throw new IOException("路线保存校验失败，请重试。");
                    main.post(() -> { if(!isDestroyed()) {route=loaded;routeGeneration++;
                        routeStatus.setText("已载入 "+loaded.size()+" 个路线点，请开启定位。");} });
                } catch(Exception error) { main.post(() -> { if(!isDestroyed()) routeStatus.setText("路线导入失败："+error.getMessage()); }); }
            });
            return;
        }
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
    private static byte[] readRouteBytes(InputStream input) throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int n;
        while((n=input.read(buffer))!=-1) {
            if(bytes.size()+n>RoutePlan.MAX_BYTES) throw new IOException("路线文件超过 512 KB");
            bytes.write(buffer,0,n);
        }
        return bytes.toByteArray();
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
            phoneSession.connect(bluetooth.getRemoteDevice(address)); return;
        }
        phoneSession.holdReconnect(30000);
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
                    store.edit().putString("watch_address", selected.getAddress()).apply(); phoneSession.connect(selected);
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
            phoneSession.clearPhoneData();
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
            emergency.rememberFix(location);
            RoutePlan.Hint hint=null;
            if(route!=null) {
                try {hint=route.update(location.getLatitude(),location.getLongitude(),location.getAccuracy());
                    routeStatus.setText(hint.text());}
                catch(IllegalArgumentException error) {routeStatus.setText(error.getMessage());}
            }
            gpsValue.setText(String.format(Locale.ROOT, "%.1f km/h   ·   %.2f km", speed, phoneDistance));
            gpsStatus.setText("定位精度约 " + Math.round(location.getAccuracy()) + " 米 · 前台同步中");
            long now = SystemClock.elapsedRealtime();
            if (gpsSending || busy || now - lastGpsSent < 5000) return;
            gpsSending = true; lastGpsSent = now; final double distance = phoneDistance; final long epoch = session;
            final long run = gpsGeneration, fix = lastGpsFix;
            final long routeRun=routeGeneration;
            final RoutePlan.Hint routeHint=hint;
            worker.execute(() -> {
                String failure = null;
                try {
                    if (gpsRunning && run == gpsGeneration && SystemClock.elapsedRealtimeNanos() - fix < GpsTracker.MAX_AGE_NS) {
                        ask(epoch, json("phone_data").put("speed", speed).put("distance", distance), "phone_data");
                        if(routeHint!=null && routeSupported && routeRun==routeGeneration) {
                            try {ask(epoch,json("nav_hint").put("direction",routeHint.direction)
                                    .put("distance_m",routeHint.distanceM).put("offroute",routeHint.offRoute?1:0),"nav_hint");}
                            catch(BleLink.DeviceError error) {
                                if(!"unknown_cmd".equals(error.getMessage())) throw error;
                                routeSupported=false;
                                main.post(() -> routeStatus.setText("手机路线可用，手表需升级后显示路线提示。"));
                            }
                        }
                    }
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
        if(code==CONNECTION_NOTIFICATION_PERMISSION)renderBridgeStatus();
        if(code==EMERGENCY_PERMISSION)renderEmergency();
    }
    @Override protected void onStop() {
        stopVoice();
        stopScan();
        if (scanDialog != null && scanDialog.isShowing()) scanDialog.dismiss();
        stopGps(true); super.onStop();
    }
    @Override protected void onDestroy() {
        stopVoice();
        session++; stopScan(); stopGps(false);
        if(phoneSession!=null)phoneSession.remove(connectionListener);
        if(emergency!=null)emergency.remove(emergencyListener);
        ioWorker.shutdownNow();
        super.onDestroy();
    }
    @Override protected void onSaveInstanceState(Bundle saved) {
        saved.putString("draft", note.getText().toString()); super.onSaveInstanceState(saved);
    }
}
