#include "../../app/velaride/velaride_activity.c"
#include <assert.h>
#include <stdio.h>
static uint8_t saved[12];
static bool present,fail;
static uint32_t now;
static int x;
ssize_t property_get_buffer(const char *key,void *data,size_t size)
{
  assert(strcmp(key,ACTIVITY_KEY)==0&&size==12);
  if(!present)return -EINVAL;
  memcpy(data,saved,12);return 12;
}
int property_set_buffer(const char *key,const void *data,size_t size)
{
  assert(strcmp(key,ACTIVITY_KEY)==0&&size==12);
  if(fail)return -EIO;
  memcpy(saved,data,12);present=true;return 0;
}
int property_commit(void){return 0;}
static void reset(uint32_t initial)
{
  memset(&g_activity,0,sizeof(g_activity));g_activity_loaded=false;g_activity_have_sample=false;
  activity_reset();present=false;fail=false;now=initial;x=0;
  velaride_activity_sample(now,600,true,false,true,0,0,16384);
}
static void feed(unsigned int seconds,bool moving)
{
  for(unsigned int i=0;i<seconds;i++) {
    now+=1000;x=moving?(x==2000?-2000:2000):0;
    velaride_activity_sample(now,600,true,false,true,x,0,16384);
  }
}
static struct velaride_activity_s state(void)
{
  struct velaride_activity_s s;velaride_activity_get(&s);return s;
}
int main(void)
{
  reset(1000);assert(state().enabled&&state().interval_min==50);
  feed(2999,false);assert(!state().pending);feed(1,false);assert(state().pending&&state().reminders==1);
  assert(velaride_activity_take_due());assert(!velaride_activity_take_due());
  feed(600,false);assert(state().reminders==1);
  assert(velaride_activity_ack(true)==0);assert(state().snooze_remaining_s==600);
  feed(599,false);assert(!state().pending);feed(1,false);assert(state().pending&&state().reminders==2);
  assert(velaride_activity_ack(false)==0);assert(state().low_activity_s==0&&state().manual_acknowledgements==1);
  assert(velaride_activity_ack(false)==-EALREADY);
  feed(2800,false);feed(30,true);feed(5,false);assert(state().low_activity_s>2800&&state().active_s==0);
  feed(120,true);assert(state().low_activity_s==0&&state().active_s>=120);
  feed(10,false);assert(state().low_activity_s>0);
  now+=1000;velaride_activity_sample(now,600,true,true,false,0,0,0);
  assert(state().riding&&!state().pending&&state().low_activity_s==0);
  now+=1000;velaride_activity_sample(now,1300,true,false,true,0,0,16384);
  assert(state().quiet&&state().low_activity_s==0);
  now+=1000;velaride_activity_sample(now,600,false,false,true,0,0,16384);assert(state().quiet);
  now+=1000;velaride_activity_sample(now,600,true,false,false,0,0,0);assert(!state().sensor_ready);
  feed(3,false);now+=10000;velaride_activity_sample(now,600,true,false,true,0,0,16384);
  assert(state().low_activity_s==0);
  assert(velaride_activity_set(false,90,2)==0);g_activity_loaded=false;
  assert(!state().enabled&&state().interval_min==90&&state().hours==2);
  fail=true;assert(velaride_activity_set(true,30,0)==-EIO);assert(!state().enabled&&state().save_error==-EIO);
  fail=false;saved[8]^=1;g_activity_loaded=false;assert(state().enabled&&state().interval_min==50);
  assert(velaride_activity_set(true,1,0)==-EINVAL);
  assert(velaride_activity_set(true,50,3)==-EINVAL);
  reset(UINT32_MAX-599999u-3000000u);feed(3000,false);assert(state().pending);
  assert(velaride_activity_ack(true)==0&&g_activity_snooze_until==0);
  feed(599,false);assert(!state().pending);feed(1,false);assert(state().pending&&state().reminders==2);
  puts("PASS: low-activity threshold, one-shot alert, snooze/ack, sustained activity, brief movement, ride/quiet/invalid suppression, CRC/save failures and uint32 rollover");
  return 0;
}
