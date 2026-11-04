#define time fake_time
#include "../../app/velaride/velaride_phone.c"
#undef time
#include <assert.h>
#include <stdio.h>
static time_t now=1793769000;
time_t fake_time(time_t *out){if(out)*out=now;return now;}
int main(void)
{
  struct velaride_phone_note_s notes[8];struct velaride_weather_s weather;
  assert(velaride_phone_notes_get(notes,8)==0);
  assert(velaride_phone_note("bad","source","title","text")==-EINVAL);
  for(unsigned int i=1;i<=9;i++) {
    char id[33];snprintf(id,sizeof(id),"%032u",i);
    assert(velaride_phone_note(id,"聊天","测试摘要","请在手机查看")==0);
  }
  assert(velaride_phone_notes_get(notes,8)==8&&notes[0].id[31]=='9'&&notes[7].id[31]=='2');
  assert(velaride_phone_take_event()&&!velaride_phone_take_event());
  assert(velaride_phone_note(notes[0].id,"聊天","更新摘要","新内容")==0);
  assert(velaride_phone_notes_get(notes,8)==8&&strcmp(notes[0].title,"更新摘要")==0);
  const char *invalid[]={"\xc0\xaf","\xe4","\xed\xa0\x80","\xf4\x90\x80\x80","bad\nsource"};
  for(unsigned int i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
    assert(velaride_phone_note(notes[0].id,invalid[i],"title","body")==-EINVAL);
  char long_body[162];memset(long_body,'x',161);long_body[161]=0;
  assert(velaride_phone_note(notes[0].id,"source","title",long_body)==-EINVAL);
  assert(velaride_weather_set("城市","晴",185,now)==0);
  assert(velaride_weather_get(&weather)&&weather.temperature_deci==185);
  velaride_phone_notes_clear();assert(!velaride_phone_take_event()&&velaride_phone_notes_get(notes,8)==0);
  assert(velaride_weather_get(&weather));
  assert(velaride_weather_set("城市","晴",1001,now)==-EINVAL);
  assert(velaride_weather_set("城市","晴",185,now-7201)==-EINVAL);
  assert(velaride_weather_set("城市","晴",185,now+601)==-EINVAL);
  now+=7201;assert(!velaride_weather_get(&weather));
  assert(velaride_weather_set("城市","雨",50,now)==0);
  now-=1000;assert(!velaride_weather_get(&weather));
  free(g_phone_notes);g_phone_notes=NULL;
  puts("PASS: bounded RAM phone inbox, dedupe/order/clear, UTF-8/length/ID rejection, independent weather units and freshness/clock checks");
  return 0;
}
