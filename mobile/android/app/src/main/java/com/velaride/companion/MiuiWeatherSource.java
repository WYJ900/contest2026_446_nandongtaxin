package com.velaride.companion;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;
import java.io.IOException;

/** Probes Xiaomi's documented weather provider; availability is ROM-dependent. */
final class MiuiWeatherSource {
    static WeatherData read(Context context) throws Exception {
        CancellationSignal cancel=new CancellationSignal();
        Handler main=new Handler(Looper.getMainLooper());Runnable timeout=cancel::cancel;
        main.postDelayed(timeout,5000);
        try {
            Cursor query;
            try {
                query=context.getContentResolver().query(Uri.parse("content://weather/actualWeatherData/2/1"),
                        new String[]{"city_name","description","temperature","temperature_unit","publish_time"},null,null,null,cancel);
            } catch(SecurityException|IllegalArgumentException error) {
                throw new IOException("当前 HyperOS 未向本 App 开放可读天气接口；请在手机天气中查看。",error);
            }
            try(Cursor cursor=query) {
            if(cursor==null||!cursor.moveToFirst())throw new IOException("系统天气未提供数据，请先打开手机天气刷新");
            return new WeatherData(cursor.getString(cursor.getColumnIndexOrThrow("city_name")),
                    cursor.getString(cursor.getColumnIndexOrThrow("description")),cursor.getString(cursor.getColumnIndexOrThrow("temperature")),
                    cursor.getInt(cursor.getColumnIndexOrThrow("temperature_unit")),Long.parseLong(cursor.getString(cursor.getColumnIndexOrThrow("publish_time"))),
                    System.currentTimeMillis()/1000);
            }
        } finally {main.removeCallbacks(timeout);}
    }
    private MiuiWeatherSource(){}
}
