#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OK 0
#define HAL_OK 0
#define ADC_USE_AVERAGE 1
#define ADC_AVERAGE_COUNT 20
#define ADC_CHAN_VBAT 7
#define LOG_ERR 3
#define syslog(...) ((void)0)
typedef struct { int dummy; } ADC_HandleTypeDef;
typedef struct { int pchnl_sel, slot_en, nchnl_sel, Channel, acc_num; } ADC_ChannelConfTypeDef;
struct adc_dev_s { void *ad_priv; };
struct callback_s { int (*au_receive)(struct adc_dev_s *, uint8_t, int32_t); };
struct adc_info_s
{
  ADC_HandleTypeDef adc_handle;
  uint8_t channel;
  float adc_vol_offset;
  float adc_vol_ratio;
  struct callback_s *cb;
};
static int g_lock;
static int callbacks;
static int32_t received;
static int index_raw;
static int config_error;
static int poll_error;
static uint32_t raw[20];
static int init_calls;
static int nxmutex_lock(int *lock) { (*lock)++; return 0; }
static int nxmutex_unlock(int *lock) { (*lock)--; return 0; }
static int HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c)
{ (void)h; assert(c->Channel == 0 && c->pchnl_sel == 7); return config_error; }
static int HAL_ADC_Init(ADC_HandleTypeDef *h)
{ (void)h; init_calls++; index_raw = 0; return 0; }
static int HAL_ADC_Start(ADC_HandleTypeDef *h) { (void)h; return 0; }
static void HAL_ADC_Stop(ADC_HandleTypeDef *h) { (void)h; }
static int HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, int timeout)
{ (void)h; (void)timeout; return poll_error; }
static uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h, int slot)
{ (void)h; assert(slot == 0); return raw[index_raw++]; }
#define ADC_FRC_EN(h) ((void)(h))
#define ADC_CLR_FRC_EN(h) ((void)(h))
#define __HAL_ADC_START_CONV(h) ((void)(h))
#define HAL_Delay_us(us) ((void)(us))

/* ACTUAL_DRIVER_FUNCTIONS */

static int receive(struct adc_dev_s *dev, uint8_t channel, int32_t value)
{ (void)dev; assert(channel == 7); received = value; callbacks++; return 0; }

int main(void)
{
  struct callback_s cb = {receive};
  struct adc_info_s info = {.channel = 7, .adc_vol_offset = 100,
                            .adc_vol_ratio = 1000, .cb = &cb};
  struct adc_dev_s dev = {&info};
  for (int i = 0; i < 20; i++) raw[i] = 2000;
  raw[0] = 0;
  raw[19] = 5000;
  assert(adc_read_work(&dev) == 0);
  assert(callbacks == 1 && received == 3800 && g_lock == 0 && init_calls == 1);
  poll_error = 3;
  assert(adc_read_work(&dev) == -ETIMEDOUT);
  assert(callbacks == 1 && g_lock == 0);
  poll_error = 0;
  config_error = 1;
  assert(adc_read_work(&dev) == -EIO);
  assert(callbacks == 1 && g_lock == 0);
  puts("PASS: ADC channel, trimmed average, divider, timeout and no false sample");
  return 0;
}
