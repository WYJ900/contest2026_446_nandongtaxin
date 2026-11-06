package com.velaride.companion;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.*;
import org.json.JSONObject;

/**
 * User-started connectedDevice foreground service. Ride GPS stays UI-only;
 * the service polls the watch safety state so fall escalation works with the
 * app closed, and may take a one-shot location for the emergency SMS.
 */
public final class WatchConnectionService extends Service {
    private static final int ID=41, LOST_ID=43;
    private static final String CHANNEL="watch_connection", LOST_CHANNEL="watch_lost";
    private static final long LOST_ALERT_MS=60000;
    private long lostSince;
    private boolean wasReady, stalled, lostAlerted;
    private PhoneLinkSession session;
    private boolean promoted;
    private EmergencyCenter emergency;
    private final Handler main=new Handler(Looper.getMainLooper());
    private volatile boolean polling;
    private long nextRetry, retryDelay=RETRY_MIN;
    private static final long RETRY_MIN=3000, RETRY_MAX=30000;
    private final BleLink.Listener listener=(ready,message)->{
        if(!ready){
            emergency.onDisconnected();
            // Firmware 57e472ce can wedge its BLE thread mid-reply; it then stops advertising until rebooted.
            if(wasReady){lostSince=SystemClock.elapsedRealtime();stalled=message!=null&&message.startsWith("手表回复超时");}
            else if(lostSince==0)lostSince=SystemClock.elapsedRealtime();
        } else {retryDelay=RETRY_MIN;lostSince=0;stalled=false;clearLost();}
        wasReady=ready;
        if(promoted)show(ready||!session.keepAlive?message:"摔倒保护暂停：手表未连接，正在自动重连（"+message+"）");
    };
    private final Runnable poll=new Runnable(){
        @Override public void run(){
            main.removeCallbacks(this);
            if(!session.keepAlive)return;
            long now=SystemClock.elapsedRealtime();
            if(!session.link.isReady()&&!session.link.isConnecting()&&now>=nextRetry&&session.reconnect()){
                nextRetry=now+retryDelay;retryDelay=Math.min(retryDelay*2,RETRY_MAX);
            }
            if(session.link.isReady()&&!polling){
                polling=true;
                session.worker.execute(()->{
                    try{
                        JSONObject status=session.link.request(new JSONObject().put("cmd","ride_status"),"ride_status");
                        emergency.onStatus(status);session.publish(status);rideEnded(status.optBoolean("active"));
                    }
                    catch(Exception ignored){/* Link errors are reported by BleLink; next poll retries. */}
                    finally{polling=false;}
                });
            }
            if(!session.link.isReady()&&lostSince!=0&&!lostAlerted&&now-lostSince>=LOST_ALERT_MS)alertLost();
            main.postDelayed(this,2000);
        }
    };
    private void alertLost() {
        lostAlerted=true;
        String text=stalled?"手表蓝牙无响应，摔倒保护已失效。请长按侧键重启手表，手机会自动重连。"
                :"手表已失联 1 分钟，摔倒保护已暂停。如手表就在身边，请重启手表。";
        PendingIntent open=PendingIntent.getActivity(this,2,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        Notification alert=new Notification.Builder(this,LOST_CHANNEL).setSmallIcon(R.drawable.ic_launcher)
                .setContentTitle(stalled?"请重启手表":"手表已失联").setContentText(text).setStyle(new Notification.BigTextStyle().bigText(text))
                .setCategory(Notification.CATEGORY_ERROR).setContentIntent(open).setAutoCancel(true).build();
        try{getSystemService(NotificationManager.class).notify(LOST_ID,alert);}catch(SecurityException ignored){}
    }
    private void clearLost() {
        if(!lostAlerted)return;
        lostAlerted=false;
        try{getSystemService(NotificationManager.class).cancel(LOST_ID);}catch(SecurityException ignored){}
    }
    private long lastActive;
    /** Ride just ended on the watch (seen active within the last minute): remember where the bike is. */
    private void rideEnded(boolean active) {
        long now=SystemClock.elapsedRealtime();
        if(active){lastActive=now;return;}
        if(lastActive!=0&&now-lastActive<60000)main.post(()->ParkingStore.capture(this,true,null));
        lastActive=0;
    }
    @Override public void onCreate(){super.onCreate();session=PhoneLinkSession.get(this);emergency=EmergencyCenter.get(this);session.add(listener);}
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent!=null&&"stop".equals(intent.getAction())){session.disconnect("已停止后台手表连接");stopSelf();return START_NOT_STICKY;}
        if(!session.keepAlive){stopSelf();return START_NOT_STICKY;}
        if(Build.VERSION.SDK_INT>=31&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED){stopSelf();return START_NOT_STICKY;}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"手表后台连接",NotificationManager.IMPORTANCE_LOW));
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(LOST_CHANNEL,"手表失联提醒",NotificationManager.IMPORTANCE_HIGH));
        Notification notification=notice(session.link.isReady()?"手表已连接，消息可在后台同步":"正在保持手表连接");
        if(Build.VERSION.SDK_INT>=29){
            int type=ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE;
            if(checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED)
                type|=ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION;
            try{startForeground(ID,notification,type);}
            catch(RuntimeException refused){startForeground(ID,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);}
        }
        else startForeground(ID,notification);
        promoted=true;main.post(poll);return START_NOT_STICKY;
    }
    private Notification notice(String text) {
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,WatchConnectionService.class).setAction("stop"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(R.drawable.ic_launcher).setContentTitle("VelaRide 手表连接")
                .setContentText(text).setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true)
                .addAction(new Notification.Action.Builder(null,"断开",stop).build()).build();
    }
    private void show(String message) {
        try{getSystemService(NotificationManager.class).notify(ID,notice(message));}catch(SecurityException ignored){}
    }
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onDestroy(){main.removeCallbacks(poll);clearLost();session.remove(listener);if(session.keepAlive){session.keepAlive=false;session.link.disconnect("后台连接服务已结束，请重新连接");}if(promoted)stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();}
}
