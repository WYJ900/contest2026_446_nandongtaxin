"""Run the LCDC1 and EXTDMA interrupts at the NuttX default NVIC priority.

NuttX arm_doirq() does not support nesting between OS-managed interrupts:
every attached IRQ must share one priority. The SiFli display code set
LCDC1 and EXTDMA to HAL priority 6 (NVIC 0xC0) while all other interrupts
are 0x80 (read back live from the watch), so a normal interrupt could
preempt the LCD/DMA handler and switch tasks from inside it. Field crash
dumps then show the LCDC1 handler left "active" (or its interrupt pending
and never taken) while threads ran, until draw_core timed out and asserted.
HAL priority 4 encodes to 0x80 (3 priority bits).
Usage: python patch_lcd_irq_priority.py <drv_lcd.c> <drv_ext_dma.c>
"""
import sys


def patch(path, old, new):
    s = open(path, encoding='utf-8', newline='').read()
    if s.count(old) != 1:
        raise SystemExit('pattern count %d in %s: %s' % (s.count(old), path, old))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
    print('patched', path)


patch(sys.argv[1], 'HAL_NVIC_SetPriority(LCDC1_IRQn, 6, 0);',
      'HAL_NVIC_SetPriority(LCDC1_IRQn, 4, 0); /* NuttX default 0x80 */')
patch(sys.argv[2], 'HAL_NVIC_SetPriority(EXTDMA_IRQn, 6, 0);',
      'HAL_NVIC_SetPriority(EXTDMA_IRQn, 4, 0); /* NuttX default 0x80 */')
