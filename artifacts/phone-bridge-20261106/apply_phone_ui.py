"""Replace the plain-text phone page from 5019760 with a card layout:
a weather pill (only when the phone has sent a valid snapshot) and one
rounded card per message (source · age / title / body). The page is only
rebuilt when its content changes, so scrolling is not reset every tick."""
import sys

p = f'{sys.argv[1]}/velaride_sport_ui.c'
s = open(p, encoding='utf-8').read()

start = s.index('  if(g_sport.info==VELARIDE_INFO_PHONE)\n    {\n      struct velaride_weather_s weather;')
end = s.index('set_label(g_sport.body,text);return;\n    }\n', start) + len('set_label(g_sport.body,text);return;\n    }\n')
s = s[:start] + '''  if (g_sport.info == VELARIDE_INFO_PHONE)
    {
      update_phone_page();
      return;
    }
''' + s[end:]

old_build = '''      g_sport.body=scroll_body("",110,258);
      button("清除手机消息",BTN_SECONDARY,70,392,250,44,phone_clear_cb,0);'''
assert s.count(old_build) == 1
s = s.replace(old_build, '''      build_phone_page();
      button("清除手机消息",BTN_SECONDARY,70,392,250,44,phone_clear_cb,0);''')

anchor = 'static void update_info(void)\n{'
assert s.count(anchor) == 1
s = s.replace(anchor, r'''static struct
{
  lv_obj_t *weather;
  lv_obj_t *weather_text;
  lv_obj_t *list;
  char signature[96];
} g_phone_page;

static lv_obj_t *phone_line(lv_obj_t *parent, const char *text, uint32_t color)
{
  lv_obj_t *obj = lv_label_create(parent);

  lv_label_set_text(obj, text);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(obj, lv_pct(100));
  lv_obj_set_style_text_font(obj, &velaride_font_cn_20, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
  return obj;
}

static void phone_note_card(const struct velaride_phone_note_s *note,
                            int64_t now)
{
  lv_obj_t *card = lv_obj_create(g_phone_page.list);
  int64_t age = now - note->received_epoch;
  char head[96];

  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(VR_TILE), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_pad_hor(card, 16, 0);
  lv_obj_set_style_pad_ver(card, 12, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  if (note->received_epoch <= 0 || age < 60)
    snprintf(head, sizeof(head), "%s · 刚刚", note->source);
  else if (age < 3600)
    snprintf(head, sizeof(head), "%s · %d 分钟前", note->source, (int)(age / 60));
  else
    snprintf(head, sizeof(head), "%s · %d 小时前", note->source, (int)(age / 3600));
  phone_line(card, head, ACCENT_COLOR);
  phone_line(card, note->title, TEXT_COLOR);
  if (note->body[0] != '\0')
    phone_line(card, note->body, MUTED_COLOR);
}

static void build_phone_page(void)
{
  memset(&g_phone_page, 0, sizeof(g_phone_page));
  g_phone_page.weather = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_phone_page.weather);
  lv_obj_set_pos(g_phone_page.weather, 18, 106);
  lv_obj_set_size(g_phone_page.weather, SPORT_W - 36, 48);
  lv_obj_set_style_bg_color(g_phone_page.weather, lv_color_hex(VR_TILE2), 0);
  lv_obj_set_style_bg_opa(g_phone_page.weather, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_phone_page.weather, 24, 0);
  lv_obj_remove_flag(g_phone_page.weather,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  g_phone_page.weather_text = phone_line(g_phone_page.weather, "", TEXT_COLOR);
  lv_obj_set_style_text_align(g_phone_page.weather_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(g_phone_page.weather_text);

  g_phone_page.list = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_phone_page.list);
  lv_obj_set_style_pad_row(g_phone_page.list, 10, 0);
  lv_obj_set_flex_flow(g_phone_page.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(g_phone_page.list, LV_DIR_VER);
  lv_obj_remove_flag(g_phone_page.list, LV_OBJ_FLAG_SCROLL_CHAIN);
}

static void update_phone_page(void)
{
  struct velaride_weather_s weather;
  struct velaride_phone_note_s notes[VELARIDE_PHONE_NOTES];
  char signature[sizeof(g_phone_page.signature)];
  bool has_weather = velaride_weather_get(&weather);
  int count = velaride_phone_notes_get(notes, VELARIDE_PHONE_NOTES);
  int64_t now = time(NULL);

  /* The page objects die with g_sport.root on any page change. */
  if (g_phone_page.list == NULL || !lv_obj_is_valid(g_phone_page.list))
    return;
  /* Minute bucket keeps "N 分钟前" fresh without rebuilding every tick. */
  snprintf(signature, sizeof(signature), "%d|%s|%lld|%lld|%lld", count,
           count > 0 ? notes[0].id : "",
           count > 0 ? (long long)notes[0].received_epoch : 0LL,
           has_weather ? (long long)weather.published_epoch : 0LL,
           (long long)(now / 60));
  if (strcmp(signature, g_phone_page.signature) == 0)
    return;
  strcpy(g_phone_page.signature, signature);

  if (has_weather)
    {
      char text[160];
      snprintf(text, sizeof(text), "%s  %.1f°C  %s", weather.city,
               weather.temperature_deci / 10.0, weather.description);
      lv_label_set_text(g_phone_page.weather_text, text);
      lv_obj_remove_flag(g_phone_page.weather, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(g_phone_page.list, 18, 166);
      lv_obj_set_size(g_phone_page.list, SPORT_W - 36, 216);
    }
  else
    {
      lv_obj_add_flag(g_phone_page.weather, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(g_phone_page.list, 18, 106);
      lv_obj_set_size(g_phone_page.list, SPORT_W - 36, 276);
    }

  lv_obj_clean(g_phone_page.list);
  if (count == 0)
    {
      lv_obj_t *empty = phone_line(g_phone_page.list,
                                   "暂无手机消息", MUTED_COLOR);
      lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_top(empty, 90, 0);
    }
  for (int i = 0; i < count; i++)
    phone_note_card(&notes[i], now);
}

static void update_info(void)
{''')

# Clearing must force a rebuild even inside the same minute.
old_clear = 'LV_UNUSED(event);velaride_phone_notes_clear();update_info();'
assert s.count(old_clear) == 1
s = s.replace(old_clear, 'LV_UNUSED(event);velaride_phone_notes_clear();g_phone_page.signature[0]=0;update_info();')
open(p, 'w', encoding='utf-8').write(s)
print('phone ui applied')
