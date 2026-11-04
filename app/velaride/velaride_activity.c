/****************************************************************************
 * Low wrist-activity reminder. This does not identify sitting or standing.
 * Pure sampling state machine; no sensor, clock, display or audio writes.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <kvdb.h>
#include "velaride_activity.h"

#define ACTIVITY_KEY "persist.velaride.activity"
#define ACTIVITY_ACTIVE_MS 120000u
#define ACTIVITY_SNOOZE_MS 600000u
static pthread_mutex_t g_activity_lock=PTHREAD_MUTEX_INITIALIZER;
static struct velaride_activity_s g_activity;
static bool g_activity_loaded;
static bool g_activity_have_sample;
static bool g_activity_due;
static uint32_t g_activity_now;
static uint32_t g_activity_last;
static uint32_t g_activity_idle_ms;
static uint32_t g_activity_active_ms;
static uint32_t g_activity_quiet_ms;
static uint32_t g_activity_snooze_until;
static bool g_activity_snoozing;
static int16_t g_activity_xyz[3];

static uint32_t activity_crc(const uint8_t *data)
{
  uint32_t crc=UINT32_MAX;
  for(int i=0;i<8;i++) {
    crc^=data[i];
    for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
  }
  return ~crc;
}
static void activity_reset(void)
{
  g_activity_idle_ms=g_activity_active_ms=g_activity_quiet_ms=0;
  g_activity_snooze_until=0;g_activity_snoozing=false;g_activity.pending=false;g_activity_due=false;
  g_activity.low_activity_s=g_activity.active_s=g_activity.snooze_remaining_s=0;
}
static void activity_load(void)
{
  uint8_t disk[12];
  if(g_activity_loaded)return;
  g_activity_loaded=true;g_activity.enabled=true;g_activity.interval_min=50;
  if(property_get_buffer(ACTIVITY_KEY,disk,sizeof(disk))!=sizeof(disk))return;
  uint32_t crc=(uint32_t)disk[8]|(uint32_t)disk[9]<<8|(uint32_t)disk[10]<<16|(uint32_t)disk[11]<<24;
  if(memcmp(disk,"VRA1",4)==0&&disk[4]<=1&&disk[5]>=20&&disk[5]<=120&&disk[6]<=2&&disk[7]==0&&crc==activity_crc(disk)) {
    g_activity.enabled=disk[4];g_activity.interval_min=disk[5];g_activity.hours=disk[6];
  }
}
void velaride_activity_get(struct velaride_activity_s *out)
{
  if(out==NULL)return;
  pthread_mutex_lock(&g_activity_lock);activity_load();*out=g_activity;pthread_mutex_unlock(&g_activity_lock);
}
int velaride_activity_set(bool enabled,uint8_t interval_min,uint8_t hours)
{
  uint8_t disk[12]={'V','R','A','1',0,0,0,0,0,0,0,0};
  if(interval_min<20||interval_min>120||hours>2)return -EINVAL;
  pthread_mutex_lock(&g_activity_lock);activity_load();
  disk[4]=enabled;disk[5]=interval_min;disk[6]=hours;
  uint32_t crc=activity_crc(disk);for(int i=0;i<4;i++)disk[8+i]=crc>>(i*8);
  int ret=property_set_buffer(ACTIVITY_KEY,disk,sizeof(disk));if(ret==0)ret=property_commit();
  g_activity.save_error=ret;
  if(ret==0) {
    g_activity.enabled=enabled;g_activity.interval_min=interval_min;g_activity.hours=hours;
    activity_reset();g_activity_have_sample=false;
  }
  pthread_mutex_unlock(&g_activity_lock);return ret;
}
void velaride_activity_sample(uint32_t now,unsigned int minute,bool time_valid,
                              bool riding,bool valid,int16_t x,int16_t y,int16_t z)
{
  pthread_mutex_lock(&g_activity_lock);activity_load();g_activity_now=now;
  g_activity.riding=riding;
  g_activity.quiet=!time_valid||minute>=1440||
    (g_activity.hours==0&&(minute<540||minute>=1260))||
    (g_activity.hours==1&&(minute<360||minute>=1320));
  g_activity.sensor_ready=valid;
  if(!g_activity.enabled||g_activity.quiet||riding||!valid) {
    activity_reset();g_activity_have_sample=false;goto out;
  }
  uint32_t dt=now-g_activity_last;
  int norm=abs((int)x)+abs((int)y)+abs((int)z);
  if(norm<32) {g_activity.sensor_ready=false;activity_reset();g_activity_have_sample=false;goto out;}
  if(!g_activity_have_sample||dt>5000) {
    activity_reset();g_activity_have_sample=true;g_activity_last=now;
    g_activity_xyz[0]=x;g_activity_xyz[1]=y;g_activity_xyz[2]=z;goto out;
  }
  if(dt==0)goto out;
  int delta=abs((int)x-g_activity_xyz[0])+abs((int)y-g_activity_xyz[1])+abs((int)z-g_activity_xyz[2]);
  bool moving=delta>=norm/25&&delta>=12;
  g_activity_xyz[0]=x;g_activity_xyz[1]=y;g_activity_xyz[2]=z;g_activity_last=now;
  if(g_activity_idle_ms<24u*60u*60u*1000u)g_activity_idle_ms+=dt;
  if(moving) {
    g_activity_quiet_ms=0;
    if(g_activity_active_ms<ACTIVITY_ACTIVE_MS)g_activity_active_ms+=dt;
  } else {
    g_activity_quiet_ms+=dt;
    if(g_activity_quiet_ms>3000)g_activity_active_ms=0;
  }
  if(g_activity_active_ms>=ACTIVITY_ACTIVE_MS) {
    g_activity_idle_ms=0;g_activity.pending=false;g_activity_due=false;g_activity_snoozing=false;
  }
  if(g_activity_snoozing&&(int32_t)(now-g_activity_snooze_until)>=0)g_activity_snoozing=false;
  if(!g_activity.pending&&!g_activity_snoozing&&
     g_activity_idle_ms>=(uint32_t)g_activity.interval_min*60000u) {
    g_activity.pending=true;g_activity_due=true;if(g_activity.reminders<UINT32_MAX)g_activity.reminders++;
  }
  g_activity.low_activity_s=g_activity_idle_ms/1000;g_activity.active_s=g_activity_active_ms/1000;
  g_activity.snooze_remaining_s=g_activity_snoozing?(g_activity_snooze_until-now+999)/1000:0;
out:
  pthread_mutex_unlock(&g_activity_lock);
}
bool velaride_activity_take_due(void)
{
  pthread_mutex_lock(&g_activity_lock);bool due=g_activity_due;g_activity_due=false;pthread_mutex_unlock(&g_activity_lock);return due;
}
int velaride_activity_ack(bool snooze)
{
  pthread_mutex_lock(&g_activity_lock);activity_load();
  if(!g_activity.pending){pthread_mutex_unlock(&g_activity_lock);return -EALREADY;}
  if(snooze) {
    g_activity_snooze_until=g_activity_now+ACTIVITY_SNOOZE_MS;
    g_activity_snoozing=true;
    g_activity.pending=false;g_activity_due=false;g_activity.snooze_remaining_s=600;
  } else {
    activity_reset();if(g_activity.manual_acknowledgements<UINT32_MAX)g_activity.manual_acknowledgements++;
  }
  pthread_mutex_unlock(&g_activity_lock);return 0;
}
