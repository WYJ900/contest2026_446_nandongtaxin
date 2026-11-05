package com.velaride.companion;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.*;
import android.widget.*;

/** Lock-screen countdown view. All state lives in EmergencyCenter. */
public final class EmergencyActivity extends Activity implements EmergencyCenter.Listener {
    private static final int RED = Color.rgb(196, 38, 38), DARK = Color.rgb(24, 8, 8), INK = Color.WHITE;
    private EmergencyCenter center;
    private TextView title, count, detail, result;
    private Button cancel, send, close;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (Build.VERSION.SDK_INT >= 27) { setShowWhenLocked(true); setTurnScreenOn(true); }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        center = EmergencyCenter.get(this);
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL); root.setBackgroundColor(DARK);
        root.setPadding(dp(24), dp(56), dp(24), dp(32));
        title = text("", 22, INK); title.setTypeface(null, Typeface.BOLD); root.addView(title);
        count = text("", 96, INK); count.setTypeface(Typeface.MONOSPACE, Typeface.BOLD); root.addView(count);
        detail = text("", 16, Color.rgb(255, 200, 200)); root.addView(detail);
        result = text("", 14, Color.rgb(230, 230, 230)); root.addView(result);
        View space = new View(this); root.addView(space, new LinearLayout.LayoutParams(1, 0, 1));
        cancel = button(root, "我没事，取消求助", Color.WHITE, RED, v -> center.cancel("已在手机上取消，本次未发送求助。"));
        send = button(root, "立即求助", RED, INK, v -> center.sendNow());
        close = button(root, "关闭", Color.rgb(60, 40, 40), INK, v -> finish());
        setContentView(root);
        handle(getIntent());
    }
    @Override protected void onNewIntent(Intent intent) { super.onNewIntent(intent); handle(intent); }
    private void handle(Intent intent) {
        String action = intent == null ? null : intent.getAction();
        if ("cancel".equals(action)) center.cancel("已在通知中取消，本次未发送求助。");
        else if ("send".equals(action)) center.sendNow();
        changed();
    }
    @Override protected void onStart() { super.onStart(); center.add(this); changed(); }
    @Override protected void onStop() { center.remove(this); super.onStop(); }

    @Override public void changed() {
        runOnUiThread(() -> {
            boolean counting = center.counting();
            title.setText(counting ? (center.drill() ? "演练：即将自动求助" : "即将自动求助") : "摔倒呼救");
            count.setText(counting ? String.valueOf(center.secondsLeft()) : "");
            count.setVisibility(counting ? View.VISIBLE : View.GONE);
            detail.setText(counting ? center.watchNote() + "\n倒计时结束后发送定位短信并拨打第一位紧急联系人" : "");
            result.setText(counting ? "" : center.status());
            cancel.setVisibility(counting ? View.VISIBLE : View.GONE);
            send.setVisibility(counting ? View.VISIBLE : View.GONE);
            close.setVisibility(counting ? View.GONE : View.VISIBLE);
        });
    }

    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setGravity(Gravity.CENTER); view.setPadding(0, dp(8), 0, dp(8)); return view;
    }
    private Button button(LinearLayout parent, String label, int fill, int ink, View.OnClickListener action) {
        Button view = new Button(this); view.setText(label); view.setAllCaps(false); view.setTextSize(20); view.setTextColor(ink);
        GradientDrawable shape = new GradientDrawable(); shape.setColor(fill); shape.setCornerRadius(dp(18)); view.setBackground(shape);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(68)); params.topMargin = dp(12);
        parent.addView(view, params); view.setOnClickListener(action); return view;
    }
}
