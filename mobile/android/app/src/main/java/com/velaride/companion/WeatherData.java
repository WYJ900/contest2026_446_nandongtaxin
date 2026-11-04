package com.velaride.companion;

import java.util.regex.*;
public final class WeatherData {
    public final String city,description;
    public final int temperatureDeci;
    public final long publishedEpoch;
    public WeatherData(String city,String description,String temperature,int unit,long published,long now) {
        this.city=WatchText.clip(city,48);this.description=WatchText.clip(description,64);
        String cleaned=temperature==null?"":temperature.replace('\u2212','-').replace('\uff0d','-');
        if(cleaned.contains("~")||cleaned.contains("～"))throw new IllegalArgumentException("系统天气不是实时温度字段");
        Matcher value=Pattern.compile("[+-]?\\d+(?:\\.\\d+)?").matcher(cleaned);
        if(!value.find()||this.city.isEmpty()||this.description.isEmpty()||(unit!=0&&unit!=1))throw new IllegalArgumentException("系统天气字段不完整");
        double degrees=Double.parseDouble(value.group());if(unit==0)degrees=(degrees-32)*5/9;
        if(!Double.isFinite(degrees)||degrees< -100||degrees>100)throw new IllegalArgumentException("系统天气温度异常");
        publishedEpoch=published>100000000000L?published/1000:published;
        if(publishedEpoch<1704067200||publishedEpoch>now+600||now-publishedEpoch>7200)throw new IllegalArgumentException("系统天气已过期，请先打开天气 App 刷新");
        temperatureDeci=(int)Math.round(degrees*10);
    }
}
