package com.velaride.companion;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.*;

/** User-started connectedDevice foreground service. GPS remains foreground UI-only. */
public final class WatchConnectionService extends Service {
    private static final int ID=41;
    private static final String CHANNEL="watch_connection";
    private PhoneLinkSession session;
    private boolean promoted;
    private final BleLink.Listener listener=(ready,message)->{if(promoted)show(message);};
    @Override public void onCreate(){super.onCreate();session=PhoneLinkSession.get(this);session.add(listener);}
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent!=null&&"stop".equals(intent.getAction())){session.disconnect("已停止后台手表连接");stopSelf();return START_NOT_STICKY;}
        if(!session.keepAlive){stopSelf();return START_NOT_STICKY;}
        if(Build.VERSION.SDK_INT>=31&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED){stopSelf();return START_NOT_STICKY;}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"手表后台连接",NotificationManager.IMPORTANCE_LOW));
        Notification notification=notice(session.link.isReady()?"手表已连接，消息可在后台同步":"正在保持手表连接");
        if(Build.VERSION.SDK_INT>=29)startForeground(ID,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        else startForeground(ID,notification);
        promoted=true;return START_NOT_STICKY;
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
    @Override public void onDestroy(){session.remove(listener);if(session.keepAlive){session.keepAlive=false;session.link.disconnect("后台连接服务已结束，请重新连接");}if(promoted)stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();}
}
