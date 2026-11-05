"""Bounded native UI reproduction; no reset, recording, sound or saved ride."""
import argparse
import codecs
import json
from pathlib import Path
import re
import time
import serial

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='COM5')
p.add_argument('--log',type=Path,required=True)
a=p.parse_args()
port=serial.Serial(port=None,baudrate=1000000,timeout=.05,write_timeout=1)
port.dtr=False;port.rts=False;port.port=a.port
decoder=codecs.getincrementaldecoder('utf-8')(errors='replace')
report={'passed':False,'scope':'Native UI automation; physical gestures and raise-to-wake remain manual','checks':[]}
transcript=[]
fatal=re.compile(r'Assertion failed|draw_core timeout|lcd_timeout_error|HardFault')
def capture(seconds,until=None):
    end=time.monotonic()+seconds;text=''
    while time.monotonic()<end:
        data=port.read(port.in_waiting or 1)
        if data:
            chunk=decoder.decode(data);text+=chunk;transcript.append(chunk)
            if fatal.search(text):raise RuntimeError('Display timeout/panic captured')
            if until and until in text:return text
    return text
def command(line,seconds,until=None):
    transcript.append('\nCOMMAND: '+line+'\n');port.write((line+'\r\n').encode())
    return capture(seconds,until)
try:
    port.open();capture(.2)
    prompt=command('',1)
    if 'vela>' in prompt:
        port.write(b'\x04');capture(1.5)
    state=command('velaride ride-status',2)
    if '"active":false' not in state:raise RuntimeError('No confirmed idle ride; skip UI bench')
    for i in range(3):
        start=time.monotonic()
        result=command('velaride honey-bench',12,'VELARIDE_HONEY_BENCH: done')
        passed='VELARIDE_HONEY_BENCH: start' in result and 'VELARIDE_HONEY_BENCH: done' in result
        check={'name':'honeycomb','cycle':i+1,'passed':passed,'seconds':round(time.monotonic()-start,3)}
        report['checks'].append(check);print(json.dumps(check),flush=True)
        if not passed:raise RuntimeError('Honeycomb benchmark incomplete')
    result=command('velaride list-bench 4',12,'VELARIDE_LIST_BENCH: done')
    passed='VELARIDE_LIST_BENCH: done' in result
    report['checks'].append({'name':'list_menu','passed':passed})
    if not passed:raise RuntimeError('List benchmark incomplete')
    result=command('velaride ui-bench',30,'VELARIDE_UI_BENCH: done')
    passed='VELARIDE_UI_BENCH: done' in result
    report['checks'].append({'name':'settings_scroll_and_pages','passed':passed})
    if not passed:raise RuntimeError('Settings benchmark incomplete')
    result=command('velaride lcd-diagnose',3)
    if 'VELARIDE_LCD:' not in result:raise RuntimeError('No LCD readback')
    report['lcd_readback']=re.findall(r'VELARIDE_LCD:.*',result)
    command('velaride wake-status',2)
    report['passed']=True
except Exception as error:
    report['error']=str(error)
finally:
    port.close();a.log.parent.mkdir(parents=True,exist_ok=True)
    raw=''.join(transcript)
    report['eof_recovered_events']=raw.count('LCDC_EOF_RECOVERED:')
    a.log.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    a.log.with_suffix('.console.txt').write_text(raw,encoding='utf-8',newline='\n')
print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
raise SystemExit(0 if report['passed'] else 1)
