"""Recover a frame whose LCDC EOF completed but whose callback never ran.

Field captures (11-04 scroll, 11-05 ride finish) both show, at the
draw_core timeout: controller idle, EOF raw status set, LCDC1 interrupt
pending, completion callback one short; the driver then asserts and the
watch halts. Only after the normal 500 ms wait has expired, check the real
hardware completion once and deliver it through the HAL handler. The
normal wait path is unchanged; without a real EOF the original timeout
handling (assert) still runs.
Usage: python patch_lcd_timeout_eof.py <drv_lcd.c>
"""
import sys

path = sys.argv[1]
s = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('pattern count %d: %s' % (s.count(old), old[:70]))
    s = s.replace(old, new)


rep('''static rt_err_t draw_core(LCD_DrvTypeDef *p_drvlcd, const uint8_t *pixels, int x0, int y0, int x1, int y1)
{''', '''/* Called only after the draw wait timed out: if the controller really
 * finished (EOF set, idle, no error), deliver the missed completion.
 */
static bool lcd_recover_missed_eof(LCD_DrvTypeDef *drv)
{
    uint32_t errors = LCD_IF_IRQ_ICB_OF_RAW_STAT |
                      LCD_IF_IRQ_DPI_UDR_RAW_STAT |
                      LCD_IF_IRQ_JDI_PAR_UDR_RAW_STAT;
    irqstate_t flags = enter_critical_section();
    uint32_t irq = drv->hlcdc.Instance->IRQ;
    bool eof = (irq & LCD_IF_IRQ_EOF_STAT) != 0 &&
               (irq & errors) == 0 &&
               (drv->hlcdc.Instance->STATUS & LCD_IF_STATUS_LCD_BUSY) == 0 &&
               drv->hlcdc.XferCpltCallback != NULL;

    if (eof)
    {
#ifdef DRV_LCD_USE_LCDC1
        NVIC_ClearPendingIRQ(LCDC1_IRQn);
#else
        NVIC_ClearPendingIRQ(LCDC2_IRQn);
#endif
        HAL_LCDC_IRQHandler(&drv->hlcdc);
    }

    leave_critical_section(flags);
    return eof && nxsem_trywait(&drv->draw_sem.sem) == 0;
}

static rt_err_t draw_core(LCD_DrvTypeDef *p_drvlcd, const uint8_t *pixels, int x0, int y0, int x1, int y1)
{''')

rep('''            err = nxsem_tickwait_uninterruptible(&p_drvlcd->draw_sem.sem,
                                                 MAX_LCD_DRAW_TIME);
            if (err == 0) p_drvlcd->draw_sem.value = 0;''', '''            err = nxsem_tickwait_uninterruptible(&p_drvlcd->draw_sem.sem,
                                                 MAX_LCD_DRAW_TIME);
            if (err != 0 && lcd_recover_missed_eof(p_drvlcd))
            {
                p_drvlcd->statistics.draw_core_err_cnt++;
                LOG_W("LCDC_EOF_RECOVERED after draw timeout (%d)",
                      (int)p_drvlcd->statistics.draw_core_err_cnt);
                err = 0;
            }
            if (err == 0) p_drvlcd->draw_sem.value = 0;''')

open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('patched', path)
