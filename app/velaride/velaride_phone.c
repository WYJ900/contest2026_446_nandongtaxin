/* Phone-owned optional weather/notification snapshots. RAM only.
 * SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "velaride_phone.h"
static pthread_mutex_t g_phone_view_lock=PTHREAD_MUTEX_INITIALIZER;
static struct velaride_phone_note_s *g_phone_notes;
static unsigned int g_phone_note_count;
static bool g_phone_note_event;
static struct velaride_weather_s g_phone_weather;
static bool g_phone_weather_valid;
static bool phone_text(const char *s,size_t max,bool newline)
{
  if(s==NULL||strlen(s)>max)return false;
  const unsigned char *p=(const unsigned char *)s;
  while(*p) {
    uint32_t c=*p++;int n;
    if(c<128) {if((c<32&&!(newline&&c==10))||c==127)return false;continue;}
    uint32_t minimum;
    if(c>=0xc2&&c<=0xdf){n=1;c&=31;minimum=128;}
    else if(c>=0xe0&&c<=0xef){n=2;c&=15;minimum=2048;}
    else if(c>=0xf0&&c<=0xf4){n=3;c&=7;minimum=65536;}
    else return false;
    for(int i=0;i<n;i++){if((*p&0xc0)!=0x80)return false;c=(c<<6)|(*p++&63);}
    if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
  }
  return true;
}
int velaride_phone_note(const char *id,const char *source,const char *title,const char *body)
{
  if(id==NULL||strlen(id)!=32||!phone_text(source,48,false)||source[0]==0||
     !phone_text(title,72,false)||title[0]==0||!phone_text(body,160,true))return -EINVAL;
  for(int i=0;i<32;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return -EINVAL;
  pthread_mutex_lock(&g_phone_view_lock);
  if(g_phone_notes==NULL)g_phone_notes=calloc(VELARIDE_PHONE_NOTES,sizeof(*g_phone_notes));
  if(g_phone_notes==NULL){pthread_mutex_unlock(&g_phone_view_lock);return -ENOMEM;}
  unsigned int at=0;
  while(at<g_phone_note_count&&strcmp(g_phone_notes[at].id,id)!=0)at++;
  if(at==g_phone_note_count&&g_phone_note_count<VELARIDE_PHONE_NOTES)g_phone_note_count++;
  if(at>=VELARIDE_PHONE_NOTES)at=VELARIDE_PHONE_NOTES-1;
  for(unsigned int i=at;i>0;i--)g_phone_notes[i]=g_phone_notes[i-1];
  struct velaride_phone_note_s *note=&g_phone_notes[0];
  memset(note,0,sizeof(*note));strcpy(note->id,id);strcpy(note->source,source);strcpy(note->title,title);strcpy(note->body,body);
  note->received_epoch=time(NULL);g_phone_note_event=true;
  pthread_mutex_unlock(&g_phone_view_lock);return 0;
}
int velaride_phone_notes_get(struct velaride_phone_note_s *notes,size_t capacity)
{
  if(notes==NULL||capacity==0)return 0;
  pthread_mutex_lock(&g_phone_view_lock);
  size_t count=g_phone_note_count<capacity?g_phone_note_count:capacity;
  if(count)memcpy(notes,g_phone_notes,count*sizeof(*notes));
  pthread_mutex_unlock(&g_phone_view_lock);return count;
}
void velaride_phone_notes_clear(void)
{
  pthread_mutex_lock(&g_phone_view_lock);
  if(g_phone_notes)memset(g_phone_notes,0,VELARIDE_PHONE_NOTES*sizeof(*g_phone_notes));
  g_phone_note_count=0;g_phone_note_event=false;
  pthread_mutex_unlock(&g_phone_view_lock);
}
bool velaride_phone_take_event(void)
{
  pthread_mutex_lock(&g_phone_view_lock);bool result=g_phone_note_event;g_phone_note_event=false;
  pthread_mutex_unlock(&g_phone_view_lock);return result;
}
int velaride_weather_set(const char *city,const char *description,int temperature_deci,int64_t epoch)
{
  int64_t now=time(NULL);
  if(!phone_text(city,48,false)||!city[0]||!phone_text(description,64,false)||!description[0]||
     temperature_deci< -1000||temperature_deci>1000||epoch<1704067200||epoch>4102444800LL||
     now<1704067200||epoch>now+600||now-epoch>7200)return -EINVAL;
  pthread_mutex_lock(&g_phone_view_lock);
  memset(&g_phone_weather,0,sizeof(g_phone_weather));strcpy(g_phone_weather.city,city);strcpy(g_phone_weather.description,description);
  g_phone_weather.temperature_deci=temperature_deci;g_phone_weather.published_epoch=epoch;g_phone_weather_valid=true;
  pthread_mutex_unlock(&g_phone_view_lock);return 0;
}
bool velaride_weather_get(struct velaride_weather_s *weather)
{
  if(weather==NULL)return false;
  int64_t now=time(NULL);
  pthread_mutex_lock(&g_phone_view_lock);
  if(now<1704067200||now<g_phone_weather.published_epoch-600||now-g_phone_weather.published_epoch>7200)g_phone_weather_valid=false;
  bool valid=g_phone_weather_valid;memset(weather,0,sizeof(*weather));if(valid)*weather=g_phone_weather;
  pthread_mutex_unlock(&g_phone_view_lock);return valid;
}
void velaride_weather_clear(void)
{
  pthread_mutex_lock(&g_phone_view_lock);memset(&g_phone_weather,0,sizeof(g_phone_weather));g_phone_weather_valid=false;
  pthread_mutex_unlock(&g_phone_view_lock);
}
