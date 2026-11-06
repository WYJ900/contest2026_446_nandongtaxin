package com.velaride.companion;

import org.json.JSONObject;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Current weather from Open-Meteo (no key) for phones whose system weather is closed to third-party apps. */
final class OnlineWeather {
    static final String HOST="api.open-meteo.com";
    private OnlineWeather() {}

    /** Coordinates are rounded to about 1 km before they leave the phone. */
    static String url(double lat,double lon) {
        return String.format(Locale.ROOT,"https://%s/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m,weather_code&timezone=auto",HOST,lat,lon);
    }

    /** WMO weather interpretation codes; wording limited to glyphs in the watch font. */
    static String describe(int code) {
        if(code==0)return "晴";
        if(code==1)return "少云";
        if(code==2)return "多云";
        if(code==3)return "阴";
        if(code==45||code==48)return "雾";
        if(code>=51&&code<=57)return "毛毛雨";
        if(code==61||code==80)return "小雨";
        if(code==63||code==81)return "中雨";
        if(code==65||code==82)return "大雨";
        if(code==66||code==67)return "冻雨";
        if(code==71||code==85)return "小雪";
        if(code==73)return "中雪";
        if(code==75||code==86)return "大雪";
        if(code==77)return "雪";
        if(code==95)return "雷阵雨";
        if(code==96||code==99)return "雷阵雨伴冰雹";
        return "天气";
    }

    static WeatherData parse(String body,String city,long now) throws Exception {
        JSONObject current=new JSONObject(body).getJSONObject("current");
        double temperature=current.getDouble("temperature_2m");
        int code=current.getInt("weather_code");
        // "current.time" is local wall time without a zone; the request time is the honest freshness.
        return new WeatherData(city==null||city.trim().isEmpty()?"当前位置":city,describe(code),
                String.format(Locale.ROOT,"%.1f",temperature),1,now,now);
    }

    static WeatherData fetch(double lat,double lon,String city) throws Exception {
        HttpURLConnection connection=(HttpURLConnection)new URL(url(lat,lon)).openConnection();
        connection.setConnectTimeout(8000);connection.setReadTimeout(8000);connection.setInstanceFollowRedirects(false);
        try {
            if(connection.getResponseCode()!=200)throw new IOException("天气服务返回 "+connection.getResponseCode());
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            try(InputStream input=connection.getInputStream()) {
                byte[] buffer=new byte[4096];int read;
                while((read=input.read(buffer))>0){out.write(buffer,0,read);if(out.size()>32768)throw new IOException("天气响应过大");}
            }
            return parse(out.toString(StandardCharsets.UTF_8.name()),city,System.currentTimeMillis()/1000);
        } finally {connection.disconnect();}
    }
}
