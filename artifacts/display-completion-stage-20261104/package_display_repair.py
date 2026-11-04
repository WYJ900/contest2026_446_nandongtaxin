from pathlib import Path
import hashlib,json,shutil,tarfile,re
root=Path(__file__).resolve().parents[1]
repo=root/'my-contest-repo';save=root/'backup/display-completion-20261104'
stage=Path('//wsl.localhost/Ubuntu-22.04/home/wyj/velaride_product_features')
out=repo/'artifacts/display-completion-stage-20261104'
ev=repo/'docs/validation-display-completion-20261104'
out.mkdir(exist_ok=True);ev.mkdir(exist_ok=True)
for n in ('firmware.bin','config','app-source.tar.gz'):shutil.copy2(save/n,out/n)
for n in ('prepare_display_repair.py','build_display_repair.sh','test_display_repair.sh','package_display_repair.py'):shutil.copy2(root/'tools'/n,out/n)
shutil.copy2(repo/'patches/lcd-completion-repair.patch',out/'lcd-completion-repair.patch')
shutil.copy2(repo/'tests/test_lcd_completion.py',out/'test_lcd_completion.py')
with tarfile.open(out/'lcd-source.tar.gz','w:gz') as z:
    n='vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd.c'
    z.add(save/'before'/n,arcname='before/'+n);z.add(stage/n,arcname='after/'+n)
    for n in ('vendor/sifli/boards/sf32lb52/drivers/lcd/drv_lcd_private.c','vendor/sifli/chips/drivers/hal/bf0_hal_lcdc.c'):
        z.add(stage/n,arcname='unchanged-reference/'+n)
for n in ('build.log','lcd-tests.log','ui-sport.log','ui-shell.log','ride-tests.log'):
    text=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',(save/n).read_text(errors='replace')).replace('\r','')
    (ev/(n.removesuffix('.log')+'.txt')).write_text('\n'.join(line.rstrip() for line in text.splitlines())+'\n',newline='\n')
panic=root/'backup/tf-repair-r2-20261104/card-in-passive.txt'
text=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',panic.read_text(errors='replace')).replace('\r','')
(ev/'no-card-scroll-panic.txt').write_text('\n'.join(line.rstrip() for line in text.splitlines())+'\n',newline='\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=root/'backup/tf-repair-20261104/before'
unchanged={n:sha(stage/n) for n in ('vendor/sifli/chips/sf32lb52/sifli_spi.c','nuttx/drivers/mmcsd/mmcsd_spi.c')}
assert all(sha(original/n)==h for n,h in unchanged.items())
m={'firmware_sha256':sha(out/'firmware.bin'),'firmware_bytes':(out/'firmware.bin').stat().st_size,
 'accepted_parent_firmware':'8027be114e60db5bbdc53b12577668cd45f5717abfa8e634701184b9a857a0c2',
 'unchanged_config_sha256':sha(out/'config'),'original_tf_driver_sha256':unchanged,
 'scope':'Check and dispatch real idle/no-error EOF while waiting for LCD completion; preserve genuine timeouts; bounded native honeycomb diagnostic',
 'host_tests':{'lcd_fault_injection':True,'actual_stage_lvgl_asan_ubsan':True,'ride_scenarios':19},
 'physical_accepted':False,'root_cause_of_missing_irq_confirmed':False,
 'raw_panic_sha256':sha(panic),'evidence_text_normalized':'ANSI/CR removed and trailing whitespace trimmed; local raw log retained',
 'source_limit':'Actual application and LCD source snapshots plus accepted archives; full clean dependency rebuild not verified'}
m['files']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'}
(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n',newline='\n')
print(m['firmware_sha256'],m['firmware_bytes'])
