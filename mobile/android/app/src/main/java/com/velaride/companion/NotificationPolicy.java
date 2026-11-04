package com.velaride.companion;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.Pattern;

/** User-selected sources, optional previews, dedupe and bounded rate; no disk log. */
public final class NotificationPolicy {
    public static final class Envelope {
        public final String id,source,title,body;
        Envelope(String id,String source,String title,String body){this.id=id;this.source=source;this.title=title;this.body=body;}
    }
    private static final Pattern SENSITIVE=Pattern.compile("验证码|校验码|动态密码|一次性密码|verification\\s*code|one[ -]time|\\botp\\b",Pattern.CASE_INSENSITIVE);
    private final LinkedHashMap<String,String> recent=new LinkedHashMap<>();
    private final Map<String,Long> sentAt=new HashMap<>();
    public synchronized Envelope accept(boolean enabled,Set<String> selected,boolean preview,String pkg,String key,
            String app,String title,String body,boolean ongoing,boolean group,boolean secret,long now) {
        if(!enabled||selected==null||!selected.contains(pkg)||ongoing||group||secret||pkg.equals("com.velaride.companion"))return null;
        if(preview&&SENSITIVE.matcher(String.valueOf(title)+" "+String.valueOf(body)).find())return null;
        Long last=sentAt.get(pkg);if(last!=null&&now>=last&&now-last<3000)return null;
        String source=WatchText.clip(app,48);if(source.isEmpty())source="手机应用";
        String heading=preview?WatchText.clip(title,72):"有新消息";
        String text=preview?WatchText.clip(body,160):"请在手机查看";
        if(heading.isEmpty())heading="新消息";
        String id=digest(pkg+":"+key),signature=heading+"\n"+text;
        if(signature.equals(recent.get(id)))return null;
        if(recent.size()>=128)recent.remove(recent.keySet().iterator().next());
        recent.put(id,signature);sentAt.put(pkg,now);
        return new Envelope(id,source,heading,text);
    }
    private static String digest(String value) {
        try {
            byte[] hash=MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8));
            StringBuilder out=new StringBuilder();for(int i=0;i<16;i++)out.append(String.format(Locale.ROOT,"%02x",hash[i]&255));
            return out.toString();
        }catch(Exception error){throw new IllegalStateException(error);}
    }
}
