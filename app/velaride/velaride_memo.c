/****************************************************************************
 * Bounded, explicitly requested onboard voice memos. No hardware at startup.
 * Reuses the accepted mic_test capture and shared audio lock unchanged.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include "velaride_memo.h"
#include "velaride_ride_session.h"

#ifndef VELARIDE_MEMO_DIR
#define VELARIDE_MEMO_DIR "/data/tf/MEMOS"
#endif
#ifndef VELARIDE_MEMO_PCM
#define VELARIDE_MEMO_PCM "/data/mic_test.pcm"
#endif
#define MEMO_PCM_BYTES (3u * 16000u * 2u)
#define MEMO_HEADER 60u
#if defined(CONFIG_ARCH_CHIP_SF32LB52) || defined(VELARIDE_MEMO_TEST)
extern int mic_test_main(int argc, char *argv[]);
extern int huangshan_audio_test_acquire(void);
extern void huangshan_audio_test_release(void);
#else
static int mic_test_main(int argc, char *argv[]) { (void)argc;(void)argv;return EXIT_FAILURE; }
static int huangshan_audio_test_acquire(void) { return -ENOSYS; }
static void huangshan_audio_test_release(void) { }
#endif

static pthread_mutex_t g_memo_lock = PTHREAD_MUTEX_INITIALIZER;
static struct velaride_memo_s g_memo;

static bool memo_storage_ready(void)
{
#ifdef VELARIDE_MEMO_TEST
  extern bool memo_test_storage_ready(void);
  return memo_test_storage_ready();
#else
  struct statfs fs;
  return statfs("/data/tf", &fs) == 0 && fs.f_type == MSDOS_SUPER_MAGIC;
#endif
}

static void memo_path(char *path, size_t size, uint32_t id)
{
  snprintf(path, size, "%s/M%07" PRIu32 ".WAV", VELARIDE_MEMO_DIR, id);
}
static void put32(uint8_t *p, uint32_t n) { for(int i=0;i<4;i++) p[i]=n>>(i*8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t memo_crc(uint32_t crc, const uint8_t *data, size_t size)
{
  for(size_t i=0;i<size;i++) {
    crc^=data[i];
    for(int b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
  }
  return crc;
}

int velaride_memo_list(uint32_t *ids, size_t capacity)
{
  DIR *dir;
  struct dirent *entry;
  int count=0;
  if(ids==NULL || capacity==0 || capacity>16) return -EINVAL;
  if(!memo_storage_ready()) return -ENODEV;
  dir=opendir(VELARIDE_MEMO_DIR);
  if(dir==NULL) return errno==ENOENT?0:-errno;
  while((entry=readdir(dir))!=NULL) {
    uint32_t id=0;
    int i,j;
    if(strlen(entry->d_name)!=12 || entry->d_name[0]!='M' || strcmp(entry->d_name+8,".WAV")!=0) continue;
    for(i=1;i<8;i++) {
      if(entry->d_name[i]<'0'||entry->d_name[i]>'9') break;
      id=id*10+entry->d_name[i]-'0';
    }
    if(i!=8 || id==0) continue;
    {
      struct stat st;
      char path[96];
      memo_path(path,sizeof(path),id);
      if(stat(path,&st)<0 || st.st_size!=MEMO_HEADER+MEMO_PCM_BYTES) continue;
    }
    for(i=0;i<count && ids[i]>id;i++);
    if(i>=(int)capacity) continue;
    if(count<(int)capacity) count++;
    for(j=count-1;j>i;j--) ids[j]=ids[j-1];
    ids[i]=id;
  }
  closedir(dir);
  return count;
}

int velaride_memo_info(uint32_t id, struct velaride_memo_s *memo)
{
  char path[96];
  uint8_t buffer[1024];
  FILE *file;
  struct stat st;
  size_t n;
  uint32_t crc=UINT32_MAX;
  if(memo==NULL || id==0 || id>9999999) return -EINVAL;
  if(!memo_storage_ready()) return -ENODEV;
  memo_path(path,sizeof(path),id);
  file=fopen(path,"rb");
  if(file==NULL) return -errno;
  if(fstat(fileno(file),&st)<0 || st.st_size!=MEMO_HEADER+MEMO_PCM_BYTES ||
     fread(buffer,1,MEMO_HEADER,file)!=MEMO_HEADER || memcmp(buffer,"RIFF",4)!=0 ||
     get32(buffer+4)!=MEMO_HEADER+MEMO_PCM_BYTES-8 || get32(buffer+16)!=16 ||
     buffer[20]!=1 || buffer[21]!=0 || buffer[22]!=1 || buffer[23]!=0 ||
     get32(buffer+24)!=16000 || get32(buffer+28)!=32000 ||
     buffer[32]!=2 || buffer[33]!=0 || buffer[34]!=16 || buffer[35]!=0 ||
     memcmp(buffer+8,"WAVEfmt ",8)!=0 || memcmp(buffer+36,"vrid",4)!=0 ||
     get32(buffer+40)!=8 || get32(buffer+48)>1 || memcmp(buffer+52,"data",4)!=0 ||
     get32(buffer+56)!=MEMO_PCM_BYTES) {fclose(file);return -EBADMSG;}
  memset(memo,0,sizeof(*memo));
  memo->id=id; memo->sequence=get32(buffer+44);memo->pending=get32(buffer+48)!=0;
  memo->bytes=MEMO_HEADER+MEMO_PCM_BYTES;memo->state=VELARIDE_MEMO_SAVED;
  rewind(file);
  while((n=fread(buffer,1,sizeof(buffer),file))>0) crc=memo_crc(crc,buffer,n);
  if(ferror(file)) {fclose(file);return -EIO;}
  fclose(file);memo->crc32=~crc;
  return 0;
}

int velaride_memo_read(uint32_t id, uint32_t offset, void *data, size_t size)
{
  char path[96];
  FILE *file;
  size_t n;
  if(id==0||id>9999999||data==NULL||size==0||size>160 || offset>MEMO_HEADER+MEMO_PCM_BYTES ||
     size>MEMO_HEADER+MEMO_PCM_BYTES-offset) return -EINVAL;
  if(!memo_storage_ready()) return -ENODEV;
  memo_path(path,sizeof(path),id);file=fopen(path,"rb");
  if(file==NULL) return -errno;
  if(fseek(file,offset,SEEK_SET)!=0) {fclose(file);return -EIO;}
  n=fread(data,1,size,file);fclose(file);
  return n==size?(int)n:-EIO;
}

static void *memo_worker(void *arg)
{
  struct velaride_memo_s memo;
  uint32_t ids[16];
  uint8_t header[MEMO_HEADER]={0},buffer[1024];
  char path[96]={0};
  char *argv[]={"mic_test","3",NULL};
  FILE *output=NULL,*input=NULL;
  int fd=-1,ret=0,count;
  bool audio_locked=false,owned=false;
  size_t n,total=0;
  (void)arg;
  velaride_memo_get(&memo);
  if(!memo_storage_ready()) {ret=-ENODEV;goto done;}
  if(mkdir(VELARIDE_MEMO_DIR,0755)<0 && errno!=EEXIST) {ret=-errno;goto done;}
  count=velaride_memo_list(ids,16);
  if(count<0) {ret=count;goto done;}
  memo.id=count>0?ids[0]+1:1;
  if(memo.id>9999999) {ret=-ENOSPC;goto done;}
  for(;memo.id<=9999999;memo.id++) {
    memo_path(path,sizeof(path),memo.id);
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd>=0 || errno!=EEXIST) break;
  }
  if(memo.id>9999999) {ret=-ENOSPC;goto done;}
  if(fd<0) {ret=-errno;goto done;}
  owned=true; output=fdopen(fd,"wb");
  if(output==NULL) {ret=-errno;close(fd);goto done;}
  /* Accepted bounded capture path owns its audio lock and disables hardware
   * before returning. Take the same lock again while copying its PCM output. */
  if(mic_test_main(2,argv)!=EXIT_SUCCESS) {ret=-EIO;goto done;}
  ret=huangshan_audio_test_acquire();
  if(ret<0) goto done;
  audio_locked=true;
  input=fopen(VELARIDE_MEMO_PCM,"rb");
  if(input==NULL) {ret=-errno;goto done;}
  memcpy(header,"RIFF",4);put32(header+4,MEMO_HEADER+MEMO_PCM_BYTES-8);
  memcpy(header+8,"WAVEfmt ",8);put32(header+16,16);header[20]=1;header[22]=1;
  put32(header+24,16000);put32(header+28,32000);header[32]=2;header[34]=16;
  memcpy(header+36,"vrid",4);put32(header+40,8);put32(header+44,memo.sequence);put32(header+48,memo.pending?1:0);
  memcpy(header+52,"data",4);put32(header+56,MEMO_PCM_BYTES);
  if(fwrite(header,1,sizeof(header),output)!=sizeof(header)) {ret=-EIO;goto done;}
  while((n=fread(buffer,1,sizeof(buffer),input))>0) {
    total+=n;
    if(total>MEMO_PCM_BYTES || fwrite(buffer,1,n,output)!=n) {ret=-EIO;goto done;}
  }
  if(ferror(input)||total!=MEMO_PCM_BYTES || fflush(output)!=0 || fsync(fileno(output))<0) ret=-EIO;
done:
  if(input!=NULL) fclose(input);
  if(output!=NULL && fclose(output)!=0) ret=-EIO;
  if(audio_locked) huangshan_audio_test_release();
  if(ret<0 && owned) unlink(path);
  if(ret==0) ret=velaride_memo_info(memo.id,&memo);
  pthread_mutex_lock(&g_memo_lock);
  if(ret==0) g_memo=memo;
  else {g_memo.state=VELARIDE_MEMO_ERROR;g_memo.error=ret;}
  pthread_mutex_unlock(&g_memo_lock);
  return NULL;
}

void velaride_memo_get(struct velaride_memo_s *memo)
{
  pthread_mutex_lock(&g_memo_lock);*memo=g_memo;pthread_mutex_unlock(&g_memo_lock);
}

int velaride_memo_start(void)
{
  struct velaride_ride_snapshot_s ride;
  struct velaride_ride_record_s latest;
  pthread_attr_t attr;
  pthread_t thread;
  int ret;
  velaride_ride_session_get(&ride);
  if(ride.active && (!ride.paused || ride.alert==VELARIDE_ALERT_IMPACT)) return -EBUSY;
  pthread_mutex_lock(&g_memo_lock);
  if(g_memo.state==VELARIDE_MEMO_RECORDING) {pthread_mutex_unlock(&g_memo_lock);return -EBUSY;}
  memset(&g_memo,0,sizeof(g_memo));
  g_memo.state=VELARIDE_MEMO_RECORDING;
  g_memo.sequence=velaride_ride_session_load_latest(&latest)==0?latest.sequence:0;
  if(ride.active) {g_memo.sequence++;g_memo.pending=true;}
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
#ifdef __NuttX__
  pthread_attr_setstacksize(&attr,8192);
#endif
  ret=pthread_create(&thread,&attr,memo_worker,NULL);
  pthread_attr_destroy(&attr);
  if(ret!=0) {g_memo.state=VELARIDE_MEMO_ERROR;g_memo.error=-ret;}
  pthread_mutex_unlock(&g_memo_lock);
  return ret==0?0:-ret;
}
