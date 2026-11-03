#define _POSIX_C_SOURCE 200809L
#define nanosleep fake_nanosleep
#define clock_gettime fake_clock_gettime
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "bf0_hal.h"
#include "../../overlays/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/watch_audio.c"

Codec fake_codec;
Pmu fake_pmu;
DmaChannel fake_dma;
DmaBase fake_dma_base;
unsigned int fake_abort_count, fake_pa_writes, fake_power, fake_amp;
bool fake_dma_owned, fake_stall, fake_transfer_error;
bool fake_power_fail, fake_codec_fail, fake_start_fail;
static uint64_t now_ms;
static uint64_t cancel_at;
static bool lock_busy;
static bool locked;
int fake_last_gain;
bool fake_live_mute, fake_live_unmute;
static int requested_gain;
static bool change_gain;
static int volume(void *arg)
{
  (void)arg;
  if (!change_gain) return requested_gain;
  return now_ms < 170 ? 6 : now_ms < 190 ? HUANGSHAN_AUDIO_MUTED : -18;
}
static uint32_t packed[1024] __attribute__((aligned(32)));

int huangshan_audio_test_acquire(void)
{if(lock_busy)return EBUSY;assert(!locked);locked=true;return 0;}
void huangshan_audio_test_release(void) {assert(locked);locked=false;}
int fake_dma_start(DMA_HandleTypeDef *d,uintptr_t src,uintptr_t dst,uint32_t words)
{
  (void)src;(void)dst;
  assert(d->Init.Mode == DMA_NORMAL);
  if(fake_start_fail)return HAL_BUSY;
  fake_dma_owned=true;d->Instance->CNDTR=words;fake_codec.IRQ=0;
  fake_dma_base.ISR=fake_transfer_error?DMA_ISR_TEIF1:0;
  return HAL_OK;
}
int fake_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
  (void)remaining;
  now_ms+=(uint64_t)delay->tv_sec*1000+delay->tv_nsec/1000000;
  if(fake_dma_owned&&!fake_stall)
    fake_dma.CNDTR=fake_dma.CNDTR>40?fake_dma.CNDTR-40:0;
  return 0;
}
int fake_clock_gettime(clockid_t id,struct timespec *out)
{(void)id;out->tv_sec=now_ms/1000;out->tv_nsec=(now_ms%1000)*1000000;return 0;}
static bool cancel(void *arg) {(void)arg;return now_ms>=cancel_at;}
static void reset(void)
{
  memset(&fake_codec,0,sizeof(fake_codec));memset(packed,0,sizeof(packed));
  packed[0]=1000;
  fake_abort_count=fake_pa_writes=fake_power=fake_amp=0;
  fake_dma_owned=fake_stall=fake_transfer_error=false;
  fake_power_fail=fake_codec_fail=fake_start_fail=false;
  lock_busy=locked=false;now_ms=0;cancel_at=UINT64_MAX;
  requested_gain=0;change_gain=false;fake_last_gain=123;
  fake_live_mute=fake_live_unmute=false;
}
static int play(void)
{return huangshan_watch_audio_play(packed,1024,1024,volume,cancel,NULL);}
static void clean(void)
{assert(!locked&&!fake_amp&&!fake_power&&!fake_dma_owned);}

int main(void)
{
  reset();assert(play()==0);assert(fake_abort_count==1);clean();
  reset();requested_gain=6;assert(play()==0);assert(fake_last_gain==6);clean();
  reset();requested_gain=12;assert(play()==-EINVAL);clean();
  reset();requested_gain=-73;assert(play()==-EINVAL);clean();
  reset();requested_gain=HUANGSHAN_AUDIO_MUTED;assert(play()==0);clean();
  reset();change_gain=true;assert(play()==0);
  assert(fake_last_gain==-18&&fake_live_mute&&fake_live_unmute);clean();
  reset();lock_busy=true;assert(play()==-EBUSY);assert(fake_pa_writes==0);clean();
  reset();fake_codec.CFG=AUDCODEC_CFG_ADC_ENABLE;
  assert(play()==-EBUSY);assert(fake_pa_writes==0);clean();
  reset();packed[0]=6001;assert(play()==-ERANGE);assert(fake_pa_writes==0);clean();
  reset();packed[600]=1;assert(play()==-ERANGE);clean();
  reset();fake_power_fail=true;assert(play()==-EIO);clean();
  reset();fake_codec_fail=true;assert(play()==-EIO);clean();
  reset();fake_start_fail=true;assert(play()==-EBUSY);assert(fake_abort_count==0);clean();
  reset();cancel_at=50;assert(play()==-ECANCELED);assert(fake_abort_count==0);clean();
  reset();cancel_at=170;assert(play()==-ECANCELED);assert(fake_abort_count==1);clean();
  reset();fake_transfer_error=true;assert(play()==-EIO);clean();
  reset();fake_stall=true;assert(play()==-ETIMEDOUT);assert(now_ms<2000);clean();
  puts("PASS: one-shot DMA, codec exclusion, buffer bounds, power/init/start/error/timeout/cancel cleanup; amplifier and codec off, lock/channel released");
}
