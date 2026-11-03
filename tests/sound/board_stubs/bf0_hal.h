#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
typedef struct { uint32_t CFG, DAC_CFG, DAC_CH0_CFG, IRQ, DAC_CH0_ENTRY; } Codec;
typedef struct { uint32_t HXT_CR1; } Pmu;
typedef struct { uint32_t CNDTR; } DmaChannel;
typedef struct { uint32_t ISR; } DmaBase;
extern Codec fake_codec;
extern Pmu fake_pmu;
extern DmaChannel fake_dma;
extern DmaBase fake_dma_base;
extern unsigned int fake_abort_count, fake_pa_writes, fake_power, fake_amp;
extern bool fake_dma_owned, fake_stall, fake_transfer_error;
extern bool fake_power_fail, fake_codec_fail, fake_start_fail;
#define hwp_audcodec (&fake_codec)
#define hwp_pmuc (&fake_pmu)
#define hwp_gpio1 ((void *)1)
#define DMA1_Channel1 (&fake_dma)
#define AUDCODEC_CFG_DAC_ENABLE 1
#define AUDCODEC_CFG_ADC_ENABLE 2
#define AUDCODEC_CFG_DAC_1K_MODE 4
#define AUDCODEC_DAC_CFG_PATH_RESET 1
#define AUDCODEC_DAC_CH0_CFG_DATA_FORMAT 1
#define AUDCODEC_DAC_CH0_CFG_DMA_EN 2
#define AUDCODEC_DAC_CH0_CFG_DOUT_MUTE_Msk 4
#define AUDCODEC_IRQ_DAC_CH0_OUT_UF 1
#define AUDCODEC_IRQ_DAC_CH0_APB_OF 2
#define AUDCODEC_IRQ_DAC_CH0_STB_OF 4
#define PMUC_HXT_CR1_BUF_AUD_EN 1
#define PAD_PA42 42
#define GPIO_A42 42
#define PIN_PULLDOWN 0
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_NOPULL 0
#define RCC_MOD_AUDCODEC 1
#define DMA_REQUEST_41 41
#define DMA_MEMORY_TO_PERIPH 1
#define DMA_PINC_DISABLE 0
#define DMA_MINC_ENABLE 1
#define DMA_PDATAALIGN_WORD 4
#define DMA_MDATAALIGN_WORD 4
#define DMA_NORMAL 0
#define DMA_PRIORITY_HIGH 2
#define DMA_IT_TC 1
#define DMA_IT_HT 2
#define DMA_IT_TE 4
#define DMA_ISR_TEIF1 8
typedef struct {
 int samplerate,clk_src_sel,clk_div,osr_sel,sinc_gain,sel_clk_dac_source;
 int diva_clk_dac,diva_clk_chop_dac,divb_clk_chop_dac,diva_clk_chop_bg;
 int diva_clk_chop_refgen,sel_clk_dac;
} AUDCODE_DAC_CLK_CONFIG_TYPE;
typedef struct {
 Codec *Instance;
 struct {struct {int opmode; AUDCODE_DAC_CLK_CONFIG_TYPE *dac_clk;} dac_cfg;} Init;
} AUDCODEC_HandleTypeDef;
typedef struct {
 DmaChannel *Instance; DmaBase *DmaBaseAddress; unsigned int ChannelIndex;
 struct {int Request,Direction,PeriphInc,MemInc,PeriphDataAlignment,MemDataAlignment,Mode,Priority,IrqPrio;} Init;
} DMA_HandleTypeDef;
typedef struct {int Pin,Mode,Pull;} GPIO_InitTypeDef;
static inline void HAL_GPIO_WritePin(void *gpio,int pin,int value)
{(void)gpio;(void)pin;fake_amp=value;fake_pa_writes++;}
static inline int HAL_GPIO_ReadPin(void *gpio,int pin)
{(void)gpio;(void)pin;return fake_amp;}
static inline void HAL_GPIO_Init(void *gpio,GPIO_InitTypeDef *config)
{(void)gpio;(void)config;}
static inline void HAL_PIN_Set(int p,int f,int pull,int h)
{(void)p;(void)f;(void)pull;(void)h;}
static inline int HAL_PMU_EnableAudio(int enabled)
{if(enabled&&fake_power_fail)return HAL_ERROR;fake_power=enabled;return HAL_OK;}
#define HAL_RCC_EnableModule(x) ((void)(x))
#define HAL_RCC_DisableModule(x) ((void)(x))
#define HAL_TURN_ON_PLL() ((void)0)
#define HAL_TURN_OFF_PLL() ((void)0)
static inline int HAL_AUDCODEC_Init(AUDCODEC_HandleTypeDef *c)
{(void)c;return fake_codec_fail?HAL_ERROR:HAL_OK;}
#define HAL_AUDCODEC_DeInit(c) ((void)(c))
#define HAL_AUDCODEC_Config_TChanel(c,n,p) ((void)(c),(void)(n),(void)(p),HAL_OK)
extern int fake_last_gain;
extern bool fake_live_mute, fake_live_unmute;
static inline void HAL_AUDCODEC_Config_DACPath(AUDCODEC_HandleTypeDef *c,int mute)
{
 if(mute)c->Instance->DAC_CH0_CFG|=AUDCODEC_DAC_CH0_CFG_DOUT_MUTE_Msk;
 else c->Instance->DAC_CH0_CFG&=~AUDCODEC_DAC_CH0_CFG_DOUT_MUTE_Msk;
 if(fake_dma_owned&&mute)fake_live_mute=true;
 if(fake_dma_owned&&!mute&&fake_live_mute)fake_live_unmute=true;
}
#define HAL_AUDCODEC_Config_DACPath_Volume(c,n,v) ((void)(c),(void)(n),fake_last_gain=(v))
#define HAL_AUDCODEC_Config_Analog_DACPath(c) ((void)(c),HAL_OK)
#define HAL_AUDCODEC_Clear_All_Channel(c,n) ((void)(c),(void)(n))
#define HAL_AUDCODEC_Close_Analog_DACPath() ((void)0)
#define __HAL_AUDCODEC_DAC_ENABLE(c) ((c)->Instance->CFG |= AUDCODEC_CFG_DAC_ENABLE)
#define __HAL_AUDCODEC_DAC_DISABLE(c) ((c)->Instance->CFG &= ~AUDCODEC_CFG_DAC_ENABLE)
#define __HAL_DMA_DISABLE_IT(d,f) ((void)(d),(void)(f))
static inline int HAL_DMA_Init(DMA_HandleTypeDef *d)
{d->DmaBaseAddress=&fake_dma_base;d->ChannelIndex=0;return HAL_OK;}
int fake_dma_start(DMA_HandleTypeDef *,uintptr_t,uintptr_t,uint32_t);
#define HAL_DMA_Start(d,s,t,n) fake_dma_start(d,s,t,n)
static inline int HAL_DMA_Abort(DMA_HandleTypeDef *d)
{(void)d;fake_abort_count++;fake_dma_owned=false;return HAL_OK;}
