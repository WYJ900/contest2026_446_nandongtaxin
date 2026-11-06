package com.velaride.companion;

import android.bluetooth.BluetoothDevice;
import android.content.*;
import android.os.SystemClock;
import org.json.JSONObject;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

/** One process-owned connection and command lane shared by UI and services. */
final class PhoneLinkSession {
    private static PhoneLinkSession instance;
    static synchronized PhoneLinkSession get(Context context) {
        if(instance==null)instance=new PhoneLinkSession(context.getApplicationContext());
        return instance;
    }
    final BleLink link;
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Context context;
    private final CopyOnWriteArrayList<BleLink.Listener> listeners=new CopyOnWriteArrayList<>();
    interface StatusListener{void status(JSONObject reply);}
    private final CopyOnWriteArrayList<StatusListener> statusListeners=new CopyOnWriteArrayList<>();
    private volatile long generation;
    volatile boolean keepAlive;
    private volatile BluetoothDevice lastDevice;
    private volatile long holdUntil;
    private volatile boolean notesSupported=true;
    private final AtomicInteger pendingNotes=new AtomicInteger();
    private PhoneLinkSession(Context context) {
        this.context=context;
        link=new BleLink(context,(ready,message)->{
            if(!ready){generation++;notesSupported=true;}
            for(BleLink.Listener listener:listeners)listener.changed(ready,message);
        });
    }
    void add(BleLink.Listener listener){listeners.addIfAbsent(listener);}
    void remove(BleLink.Listener listener){listeners.remove(listener);}
    void addStatus(StatusListener listener){statusListeners.addIfAbsent(listener);}
    void removeStatus(StatusListener listener){statusListeners.remove(listener);}
    /** Each ride_status reply, from the service poll or a UI refresh; called on the BLE worker. */
    void publish(JSONObject reply){for(StatusListener listener:statusListeners)listener.status(reply);}
    void connect(BluetoothDevice device) {
        try {
            keepAlive=true;lastDevice=device;holdUntil=0;
            context.startForegroundService(new Intent(context,WatchConnectionService.class));
            link.connect(device);
        } catch(RuntimeException error) {
            keepAlive=false;link.disconnect("后台蓝牙服务无法启动，请检查附近设备权限与后台设置。");
            context.stopService(new Intent(context,WatchConnectionService.class));
        }
    }
    /** Service retry after a drop, so fall protection does not silently stop. */
    boolean reconnect() {
        BluetoothDevice device=lastDevice;
        if(!keepAlive||device==null||link.isReady()||link.isConnecting()
                ||SystemClock.elapsedRealtime()<holdUntil)return false;
        try{link.connect(device);return true;}catch(RuntimeException error){return false;}
    }
    /** A connected watch stops advertising; keep the retry away while the user scans. */
    void holdReconnect(long millis){holdUntil=SystemClock.elapsedRealtime()+millis;}
    void disconnect(String reason) {
        keepAlive=false;link.disconnect(reason);
        context.stopService(new Intent(context,WatchConnectionService.class));
    }
    void clearPhoneData() {
        final long epoch=generation;
        worker.execute(()->{
            if(epoch!=generation||!link.isReady())return;
            try{link.request(new JSONObject().put("cmd","phone_clear"),"phone_clear");}
            catch(Exception ignored){/* Native 15-second expiry remains the fallback. */}
        });
    }
    void relay(NotificationPolicy.Envelope note) {
        final long epoch=generation,created=SystemClock.elapsedRealtime();
        if(!link.isReady()||!notesSupported){android.util.Log.i("VelaRideNote","relay skip ready="+link.isReady()+" supported="+notesSupported);return;}
        if(pendingNotes.incrementAndGet()>8){pendingNotes.decrementAndGet();return;}
        worker.execute(()->{
            try {
                if(epoch!=generation||!link.isReady()||!notesSupported||SystemClock.elapsedRealtime()-created>60000)return;
                JSONObject reply=link.request(new JSONObject().put("cmd","phone_note").put("id",note.id)
                        .put("source",note.source).put("title",note.title).put("body",note.body),"phone_note");
                android.util.Log.i("VelaRideNote","watch reply "+reply);
                context.getSharedPreferences("phone-bridge",Context.MODE_PRIVATE).edit()
                        .putString("relay_status",reply.optBoolean("ok")?"手机消息通道正常":"手机消息发送未完成").apply();
            } catch(BleLink.DeviceError error) {
                android.util.Log.i("VelaRideNote","watch error "+error.getMessage());
                if("unknown_cmd".equals(error.getMessage()))notesSupported=false;
                context.getSharedPreferences("phone-bridge",Context.MODE_PRIVATE).edit()
                        .putString("relay_status",notesSupported?"消息未发出，请检查连接":"手表需升级后支持手机消息").apply();
            } catch(Exception error) {
                android.util.Log.i("VelaRideNote","relay failed "+error);
                context.getSharedPreferences("phone-bridge",Context.MODE_PRIVATE).edit()
                        .putString("relay_status","手机消息连接中断，重连后接收新消息").apply();
            } finally {pendingNotes.decrementAndGet();}
        });
    }
}
