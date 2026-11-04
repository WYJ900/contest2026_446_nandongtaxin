package com.velaride.companion;

import android.app.Notification;
import android.content.*;
import android.os.*;
import android.service.notification.*;
import java.util.*;

/** OS notification access is granted by the user in system settings. */
public final class PhoneNotificationService extends NotificationListenerService {
    private final NotificationPolicy policy=new NotificationPolicy();
    @Override public void onNotificationPosted(StatusBarNotification posted) {
        if(posted==null)return;
        SharedPreferences settings=getSharedPreferences("phone-bridge",MODE_PRIVATE);
        boolean enabled=settings.getBoolean("relay_enabled",false);
        Set<String> selected=settings.getStringSet("selected_apps",Collections.emptySet());
        if(!enabled||!selected.contains(posted.getPackageName()))return;
        long age=System.currentTimeMillis()-posted.getPostTime();if(age< -10000||age>120000)return;
        Notification notification=posted.getNotification();
        boolean preview=settings.getBoolean("previews",false);
        String title="",body="";
        if(preview) {
            CharSequence a=notification.extras.getCharSequence(Notification.EXTRA_TITLE),b=notification.extras.getCharSequence(Notification.EXTRA_TEXT);
            title=a==null?"":a.toString();body=b==null?"":b.toString();
        }
        String app=posted.getPackageName();
        try{app=getPackageManager().getApplicationLabel(getPackageManager().getApplicationInfo(posted.getPackageName(),0)).toString();}
        catch(Exception ignored){}
        NotificationPolicy.Envelope note=policy.accept(enabled,selected,preview,posted.getPackageName(),posted.getKey(),app,title,body,
                (notification.flags&Notification.FLAG_ONGOING_EVENT)!=0,(notification.flags&Notification.FLAG_GROUP_SUMMARY)!=0,
                notification.visibility==Notification.VISIBILITY_SECRET,SystemClock.elapsedRealtime());
        if(note!=null)PhoneLinkSession.get(this).relay(note);
    }
}
