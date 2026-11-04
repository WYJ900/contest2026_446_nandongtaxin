package com.velaride.companion;
import java.nio.charset.StandardCharsets;
import java.util.*;
public final class PhoneBridgeTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    private static NotificationPolicy.Envelope note(NotificationPolicy p,boolean enabled,boolean preview,String key,String title,String body,long now) {
        return p.accept(enabled,Collections.singleton("chat"),preview,"chat",key,"聊天",title,body,false,false,false,now);
    }
    public static void main(String[] args) {
        NotificationPolicy p=new NotificationPolicy();
        check(note(p,false,true,"a","名字","内容",1000)==null);
        NotificationPolicy.Envelope generic=note(p,true,false,"a","私人名字","私人消息 12345",1000);
        check(generic!=null&&generic.id.length()==32&&generic.title.equals("有新消息")&&!generic.body.contains("私人"));
        check(note(p,true,true,"b","标题","内容",2000)==null);
        check(note(p,true,false,"a","变化","仍然不应泄露",5000)==null);
        check(note(p,true,true,"a","标题","新内容",5000)!=null);
        check(note(p,true,true,"otp","验证码","123456",9000)==null);
        check(p.accept(true,Collections.singleton("chat"),true,"chat","x","app","a","b",true,false,false,9000)==null);
        check(p.accept(true,Collections.singleton("chat"),true,"chat","x","app","a","b",false,true,false,9000)==null);
        check(p.accept(true,Collections.singleton("chat"),true,"chat","x","app","a","b",false,false,true,9000)==null);
        check(p.accept(true,Collections.singleton("other"),true,"chat","x","app","a","b",false,false,false,9000)==null);
        String clipped=WatchText.clip("中文🙂\n"+String.join("",Collections.nCopies(100,"文")),72);
        check(clipped.getBytes(StandardCharsets.UTF_8).length<=72&&!clipped.contains("🙂")&&!clipped.contains("\n"));
        long now=1793769000;
        WeatherData c=new WeatherData("城市","多云","18.5℃",1,now*1000,now);
        check(c.temperatureDeci==185&&c.publishedEpoch==now);
        check(new WeatherData("城市","晴","50F",0,now,now).temperatureDeci==100);
        check(new WeatherData("城市","雪","−5℃",1,now,now).temperatureDeci==-50);
        for(long published:new long[]{now-7201,now+601,0}) {
            try{new WeatherData("城市","晴","18",1,published,now);throw new AssertionError();}
            catch(IllegalArgumentException expected){}
        }
        for(String bad:new String[]{"NaN","999℃","8~20℃"}) {
            try{new WeatherData("城市","晴",bad,1,now,now);throw new AssertionError();}
            catch(IllegalArgumentException expected){}
        }
        System.out.println("PASS: selected-app/private preview policy, OTP/secret/ongoing/group filters, dedupe/rate, watch glyph/UTF8 caps, weather units/freshness/validation");
    }
}
