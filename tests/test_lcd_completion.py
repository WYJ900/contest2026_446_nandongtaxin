"""Fault injection of the actual LCD EOF/timeout wait helper."""
from pathlib import Path
import os
import subprocess
import tempfile

stage=Path(os.environ.get('VELARIDE_STAGE','/home/wyj/velaride_product_features'))
s=(stage/'vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd.c').read_text()
start=s.index('static int lcd_wait_draw_complete(')
brace=s.index('{',start);end=brace+1;depth=1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define clock_t uint32_t
typedef unsigned irqstate_t;
#define MAX_LCD_DRAW_TIME 50u
#define MSEC2TICK(x) (x)
#define DRV_LCD_USE_LCDC1
#define LCDC1_IRQn 63
#define LCD_IF_IRQ_EOF_STAT 1u
#define LCD_IF_IRQ_ICB_OF_RAW_STAT (1u<<17)
#define LCD_IF_IRQ_DPI_UDR_RAW_STAT (1u<<19)
#define LCD_IF_IRQ_JDI_PAR_UDR_RAW_STAT (1u<<21)
#define LCD_IF_STATUS_LCD_BUSY 1u
struct regs { uint32_t IRQ,STATUS; };
typedef struct { struct regs *Instance; void (*XferCpltCallback)(void); } HAL;
typedef struct { HAL hlcdc; struct { int sem; } draw_sem; } LCD_DrvTypeDef;
static uint32_t now,ready_at;
static int critical,handled,cleared,warnings;
static bool post_event,no_post;
static int wait_error;
static LCD_DrvTypeDef *owner;
static uint32_t clock_systime_ticks(void){return now;}
static irqstate_t enter_critical_section(void){assert(!critical);critical=1;return 0;}
static void leave_critical_section(irqstate_t flags){assert(!flags&&critical);critical=0;}
static int nxsem_tickwait_uninterruptible(int *sem,uint32_t timeout)
{
    assert(!critical&&timeout>0&&timeout<=10);
    if(*sem){--*sem;return 0;}
    if(wait_error)return wait_error;
    now+=timeout;
    if(ready_at&&now>=ready_at)owner->hlcdc.Instance->STATUS=0;
    if(post_event){post_event=false;*sem=1;}
    return -ETIMEDOUT;
}
static int nxsem_trywait(int *sem){assert(!critical);if(*sem){--*sem;return 0;}return -EAGAIN;}
static void NVIC_ClearPendingIRQ(int irq){assert(critical&&irq==63);cleared++;}
static void HAL_LCDC_IRQHandler(HAL *h)
{
    assert(critical&&(h->Instance->IRQ&1)&&!h->Instance->STATUS);
    handled++;h->Instance->IRQ=0;
    if(!no_post)owner->draw_sem.sem++;
}
#define LOG_W(...) (warnings++)
static void callback(void){}
'''
code+='\n'+s[start:end]+'\n'
code+=r'''
static LCD_DrvTypeDef setup(struct regs *r)
{
    now=ready_at=0;critical=handled=cleared=warnings=wait_error=0;
    post_event=no_post=false;r->IRQ=0;r->STATUS=0;
    LCD_DrvTypeDef d={.hlcdc={r,callback}};return d;
}
int main(void)
{
    struct regs r;LCD_DrvTypeDef d=setup(&r);owner=&d;
    d.draw_sem.sem=1;assert(lcd_wait_draw_complete(&d)==0&&!handled&&now==0);
    d=setup(&r);r.IRQ=1;assert(lcd_wait_draw_complete(&d)==0);
    assert(handled==1&&cleared==1&&warnings==1&&d.draw_sem.sem==0&&!critical);
    d=setup(&r);post_event=true;assert(lcd_wait_draw_complete(&d)==0&&!handled);
    d=setup(&r);r.IRQ=1;r.STATUS=1;ready_at=30;
    assert(lcd_wait_draw_complete(&d)==0&&now==30&&handled==1);
    for(unsigned err=1u<<17;err<=(1u<<21);err<<=2) {
        d=setup(&r);r.IRQ=1|err;
        assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&!handled&&!cleared&&now==50);
    }
    d=setup(&r);r.IRQ=1;r.STATUS=1;assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&!handled);
    d=setup(&r);assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&!handled);
    d=setup(&r);r.IRQ=1;d.hlcdc.XferCpltCallback=NULL;
    assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&!handled);
    d=setup(&r);r.IRQ=1;no_post=true;
    assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&handled==1&&!warnings);
    d=setup(&r);wait_error=-EINVAL;
    assert(lcd_wait_draw_complete(&d)==-EINVAL&&!handled&&now==0);
    d=setup(&r);now=UINT32_MAX-20;
    assert(lcd_wait_draw_complete(&d)==-ETIMEDOUT&&!handled);
    puts("PASS: real semaphore completion, pending EOF, late IRQ, busy/error/missing-event rejection, no synthetic success, original timeout/errors and tick wrap");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
