"""Isolate LCD completion handling on the accepted application/TF driver."""
from pathlib import Path
import difflib
import tarfile
import shutil

root=Path(__file__).resolve().parents[1]
repo=root/'my-contest-repo'
stage=Path('//wsl.localhost/Ubuntu-22.04/home/wyj/velaride_product_features')
save=root/'backup/display-completion-20261104'
save.mkdir(parents=True,exist_ok=True)
for name in ('vendor/sifli/chips/sf32lb52/sifli_spi.c','nuttx/drivers/mmcsd/mmcsd_spi.c'):
    shutil.copy2(root/'backup/tf-repair-20261104/before'/name,stage/name)
app=stage/'apps/examples'
with tarfile.open(repo/'artifacts/install-stage-20261104/app-source.tar.gz') as z:
    for m in z.getmembers():
        if m.isfile():
            p=app/m.name
            assert p.resolve().is_relative_to((app/'velaride').resolve())
            p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(z.extractfile(m).read())
name='vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd.c'
before=save/'before'/name
before.parent.mkdir(parents=True,exist_ok=True)
if not before.exists():before.write_bytes((stage/name).read_bytes())
old=before.read_text()
marker='static rt_err_t draw_core('
pos=old.index(marker)
helper='''/* A completed frame may still have an undelivered EOF interrupt. Recheck
 * the real hardware event before declaring a timeout. Only the normal HAL
 * callback can post draw_sem; an idle controller alone is never completion.
 */
static int lcd_wait_draw_complete(LCD_DrvTypeDef *drv)
{
    clock_t start = clock_systime_ticks();
    clock_t elapsed = 0;
    int ret;

    while (elapsed < MAX_LCD_DRAW_TIME)
    {
        clock_t slice = MSEC2TICK(10);
        if (slice < 1) slice = 1;
        if (slice > MAX_LCD_DRAW_TIME - elapsed)
            slice = MAX_LCD_DRAW_TIME - elapsed;
        ret = nxsem_tickwait_uninterruptible(&drv->draw_sem.sem, slice);
        if (ret == 0 || ret != -ETIMEDOUT) return ret;

        irqstate_t flags = enter_critical_section();
        uint32_t irq = drv->hlcdc.Instance->IRQ;
        uint32_t errors = LCD_IF_IRQ_ICB_OF_RAW_STAT |
                          LCD_IF_IRQ_DPI_UDR_RAW_STAT |
                          LCD_IF_IRQ_JDI_PAR_UDR_RAW_STAT;
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
        if (eof && nxsem_trywait(&drv->draw_sem.sem) == 0)
        {
            LOG_W("LCDC_EOF_RECOVERED: real completion callback received");
            return 0;
        }
        elapsed = clock_systime_ticks() - start;
    }
    return -ETIMEDOUT;
}

'''
new=old[:pos]+helper+old[pos:]
needle='''            err = nxsem_tickwait_uninterruptible(&p_drvlcd->draw_sem.sem,
                                                 MAX_LCD_DRAW_TIME);'''
assert new.count(needle)==1
new=new.replace(needle,'            err = lcd_wait_draw_complete(p_drvlcd);',1)
needle='''            rt_err_t err2 = nxsem_post(&p_drvlcd->draw_sem.sem);
            RT_ASSERT(RT_EOK == err2);'''
assert new.count(needle)==1
new=new.replace(needle,'            /* Do not manufacture a completion after a real timeout. */',1)
(stage/name).write_text(new,newline='\n')
shell=app/'velaride/velaride_shell.c'
before_shell=save/'before-shell.c'
if not before_shell.exists():before_shell.write_bytes(shell.read_bytes())
s=shell.read_text()
s=s.replace('static volatile int g_list_bench_requested;', 'static volatile int g_list_bench_requested;\nstatic volatile bool g_honey_bench_requested;',1)
at=s.index('static void list_bench_step(void)')
s=s[:at]+'''/* Bounded, UI-thread-only reproduction of the native honeycomb redraw. */
static void honey_bench_step(void)
{
  static const int points[][2]={{0,0},{-160,120},{180,-140},{-180,-120},{160,140},{0,0}};
  static int step=-1;
  static uint32_t since;
  if(g_honey_bench_requested)
    {
      g_honey_bench_requested=false;
      struct velaride_ride_snapshot_s ride;
      velaride_ride_session_get(&ride);
      if(ride.active||velaride_sport_ui_active()) return;
      screen_wake(false);menu_show();g_menu.list_mode=false;
      lv_obj_add_flag(g_menu.list,LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_menu.root,LV_OBJ_FLAG_HIDDEN);
      g_menu.dragging=true;step=0;since=lv_tick_get();
      printf("VELARIDE_HONEY_BENCH: start\\n");
    }
  if(step<0||lv_tick_elaps(since)<16) return;
  since=lv_tick_get();lv_display_trigger_activity(NULL);
  if(step>=150)
    {
      g_menu.dragging=false;menu_hide();step=-1;
      printf("VELARIDE_HONEY_BENCH: done samples=150\\n");return;
    }
  int segment=step/30;float t=(step%30)/30.0f;
  honey_move(points[segment][0]*(1-t)+points[segment+1][0]*t,
             points[segment][1]*(1-t)+points[segment+1][1]*t);
  step++;
}

'''+s[at:]
s=s.replace('  list_bench_step();','  honey_bench_step();\n  list_bench_step();',1)
at=s.index('  if (argc > 1 && strcmp(argv[1], "ui-bench") == 0)')
s=s[:at]+'''  if (argc == 2 && strcmp(argv[1], "honey-bench") == 0)
    {
      g_honey_bench_requested=true;
      return EXIT_SUCCESS;
    }

'''+s[at:]
shell.write_text(s,newline='\n')
patch=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
patch+=''.join(difflib.unified_diff(before_shell.read_text().splitlines(True),s.splitlines(True),fromfile='a/apps/examples/velaride/velaride_shell.c',tofile='b/apps/examples/velaride/velaride_shell.c'))
(repo/'patches/lcd-completion-repair.patch').write_text(patch,newline='\n')
print('Original TF drivers restored; LCD completion handling and native honeycomb diagnostic prepared.')
