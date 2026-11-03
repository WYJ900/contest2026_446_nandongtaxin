package com.velaride.companion;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;

public final class RideReviewTest {
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        RideHistory.Entry old = new RideHistory.Entry("a",11,60,40,10,1,0,1,1000,1000,"");
        String unknown = RideReview.local(old, Collections.emptyList());
        check(unknown.contains("本地复盘") && unknown.contains("没有有效 GPS") && unknown.contains("旧记录"));
        check(!unknown.contains("最高 0") && !unknown.contains("已通知"));
        RideHistory.Entry current = new RideHistory.Entry("a",12,100,60,20,2,1,3,2000,2000,"",
                new RideGps(10000,50,1800,2000),new RideSafety(1,2,1,0,1,1,3,90));
        String text = RideReview.local(current, Arrays.asList(old,current));
        check(text.contains("0.050 km") && text.contains("18.00 km/h") && text.contains("有效采样"));
        check(text.contains("求助意向") && text.contains("不表示已发送") && text.contains("记录 #11"));
        check(text.contains("不代表骑行发生顺序") && text.contains("40 秒"));
        check(RideReview.identity(current).equals(RideReview.identity(current.withNote("新备注"))));
        check(!RideReview.identity(current).equals(RideReview.identity(old)));
        String watch = RideReview.watchText("fallback",String.join("",Collections.nCopies(200,"骑行🚲")));
        check(watch.startsWith("本地复盘：") && watch.getBytes(StandardCharsets.UTF_8).length <= 260);
        check(!Character.isHighSurrogate(watch.charAt(watch.length()-1)));
        check(RideReview.watchText("mimo","完成").startsWith("AI复盘："));
        System.out.println("PASS: evidence-only local review, missing data, labelled comparisons, identity and UTF-8 watch byte limits");
    }
}
