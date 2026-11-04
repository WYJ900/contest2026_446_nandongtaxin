#define VELARIDE_MEMO_TEST
static char test_dir[128];
static char test_pcm[128];
#define VELARIDE_MEMO_DIR test_dir
#define VELARIDE_MEMO_PCM test_pcm
#include "../../app/velaride/velaride_memo.c"
#include <assert.h>

static bool ready=true;
static int captures;
static bool capture_fail;
static struct velaride_ride_snapshot_s ride={.active=true,.paused=true};
bool memo_test_storage_ready(void) {return ready;}
void velaride_ride_session_get(struct velaride_ride_snapshot_s *out) {*out=ride;}
int velaride_ride_session_load_latest(struct velaride_ride_record_s *out) {memset(out,0,sizeof(*out));out->sequence=11;return 0;}
int huangshan_audio_test_acquire(void) {return 0;}
void huangshan_audio_test_release(void) {}
int mic_test_main(int argc,char *argv[]) {
  assert(argc==2&&strcmp(argv[1],"3")==0);captures++;
  if(capture_fail)return EXIT_FAILURE;
  FILE *file=fopen(test_pcm,"wb");assert(file);
  for(unsigned i=0;i<MEMO_PCM_BYTES;i++)assert(fputc(i&255,file)!=EOF);
  assert(fclose(file)==0);return 0;
}
static struct velaride_memo_s wait_done(void) {
  struct velaride_memo_s state;
  for(int i=0;i<5000;i++) {velaride_memo_get(&state);if(state.state!=VELARIDE_MEMO_RECORDING)return state;usleep(1000);}
  assert(!"memo worker timeout");return state;
}
int main(void) {
  char temp[]="/tmp/velaride-memo-data.XXXXXX";
  assert(mkdtemp(temp));snprintf(test_dir,sizeof(test_dir),"%s/MEMOS",temp);snprintf(test_pcm,sizeof(test_pcm),"%s/input.pcm",temp);
  ride.paused=false;assert(velaride_memo_start()==-EBUSY&&captures==0);ride.paused=true;
  ready=false;assert(velaride_memo_start()==0);assert(wait_done().error==-ENODEV&&captures==0);ready=true;
  assert(velaride_memo_start()==0);struct velaride_memo_s state=wait_done();
  assert(state.state==VELARIDE_MEMO_SAVED&&state.id==1&&state.sequence==12&&state.pending&&captures==1);
  assert(state.bytes==96060);
  uint8_t buffer[160];assert(velaride_memo_read(1,0,buffer,60)==60);
  assert(memcmp(buffer,"RIFF",4)==0&&get32(buffer+44)==12&&get32(buffer+48)==1&&get32(buffer+56)==96000);
  uint32_t crc=UINT32_MAX;
  for(uint32_t offset=0;offset<state.bytes;) {
    size_t n=state.bytes-offset>160?160:state.bytes-offset;
    assert(velaride_memo_read(1,offset,buffer,n)==(int)n);crc=memo_crc(crc,buffer,n);offset+=n;
  }
  assert(~crc==state.crc32);
  assert(velaride_memo_read(1,state.bytes,buffer,1)==-EINVAL);
  assert(velaride_memo_read(1,0,buffer,161)==-EINVAL);
  assert(velaride_memo_start()==0);state=wait_done();assert(state.id==2);
  char partial[256];snprintf(partial,sizeof(partial),"%s/M0000003.WAV",test_dir);
  FILE *unfinished=fopen(partial,"wb");assert(unfinished);fputc(7,unfinished);fclose(unfinished);
  assert(velaride_memo_start()==0);state=wait_done();assert(state.id==4);
  struct stat partial_stat;assert(stat(partial,&partial_stat)==0&&partial_stat.st_size==1);
  capture_fail=true;assert(velaride_memo_start()==0);assert(wait_done().state==VELARIDE_MEMO_ERROR);
  uint32_t ids[16];assert(velaride_memo_list(ids,16)==3&&ids[0]==4&&ids[1]==2&&ids[2]==1);
  char path[256];snprintf(path,sizeof(path),"%s/M0000001.WAV",test_dir);FILE *file=fopen(path,"r+b");assert(file);fputc('X',file);fclose(file);
  assert(velaride_memo_info(1,&state)==-EBADMSG);
  unlink(path);snprintf(path,sizeof(path),"%s/M0000002.WAV",test_dir);unlink(path);
  snprintf(path,sizeof(path),"%s/M0000004.WAV",test_dir);unlink(path);unlink(partial);
  unlink(test_pcm);rmdir(test_dir);rmdir(temp);
  puts("PASS: bounded memo worker, active-ride guard, missing storage, WAV/ride hint/CRC/chunks, no-overwrite and failure cleanup");
  return 0;
}
