from pathlib import Path
import hashlib
import json
import shutil
import tarfile

root=Path(__file__).resolve().parents[1]
repo=root/'my-contest-repo'
save=root/'backup/tf-repair-r2-20261104'
before=root/'backup/tf-repair-20261104'
stage=Path('//wsl.localhost/Ubuntu-22.04/home/wyj/velaride_product_features')
out=repo/'artifacts/tf-repair-r2-stage-20261104'
evidence=repo/'docs/validation-tf-repair-r2-20261104'
out.mkdir(exist_ok=True);evidence.mkdir(exist_ok=True)
for name in ('firmware.bin','config'):
    shutil.copy2(save/name,out/name)
shutil.copy2(before/'before-driver-sha256.json',out/'before-driver-sha256.json')
shutil.copy2(repo/'patches/tf-spi-recovery.patch',out/'tf-spi-recovery.patch')
for name in ('prepare_tf_repair.py','build_tf_repair.sh','test_tf_repair.sh','package_tf_repair.py'):
    shutil.copy2(root/'tools'/name,out/name)
shutil.copy2(repo/'tests/test_tf_spi_recovery.py',out/'test_tf_spi_recovery.py')
with tarfile.open(out/'driver-source.tar.gz','w:gz') as archive:
    for name in ('vendor/sifli/chips/sf32lb52/sifli_spi.c','nuttx/drivers/mmcsd/mmcsd_spi.c'):
        archive.add(before/'before'/name,arcname='before/'+name)
        archive.add(stage/name,arcname='after/'+name)
for name in ('build.log','configure.log','driver-tests.log','ui-sport.log','ui-shell.log','ride-tests.log','checkpatch-file.log'):
    shutil.copy2((save if (save/name).exists() else before)/name,evidence/(name.removesuffix('.log')+'.txt'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest={'firmware_sha256':sha(out/'firmware.bin'),'firmware_bytes':(out/'firmware.bin').stat().st_size,
 'base_firmware':'8027be114e60db5bbdc53b12577668cd45f5717abfa8e634701184b9a857a0c2',
 'app_source':'../install-stage-20261104/app-source.tar.gz','unchanged_static_config':True,
 'scope':'Non-DMA SPI synchronous single words (SDK HAL limit), idle-byte initialization, bounded timeout cleanup; bounded MMC initialization trace',
 'driver_fault_injection_passed':True,'accepted_app_lvgl_asan_ubsan_passed':True,'ride_scenarios':19,
 'hardware_verified':False,'source_limit':'Accepted actual application archive plus actual before/after driver files; full clean dependency rebuild not verified'}
manifest['files']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',newline='\n')
print(json.dumps(manifest,indent=2))
