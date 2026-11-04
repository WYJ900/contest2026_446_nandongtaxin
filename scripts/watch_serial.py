"""Bounded passive console capture, tolerating a physical USB power cycle."""
import argparse
import codecs
from pathlib import Path
import time
import serial

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='COM5')
p.add_argument('--seconds',type=float,default=120)
p.add_argument('--log',type=Path,required=True)
a=p.parse_args()
if not 0<a.seconds<=300:
    p.error('--seconds must be 0..300')
a.log.parent.mkdir(parents=True,exist_ok=True)
end=time.monotonic()+a.seconds
port=None
decoder=codecs.getincrementaldecoder('utf-8')(errors='replace')
with a.log.open('w',encoding='utf-8',newline='\n') as log:
    def emit(text):
        log.write(text);log.flush();print(text,end='',flush=True)
    try:
        while time.monotonic()<end:
            if port is None:
                candidate=serial.Serial(port=None,baudrate=1000000,timeout=.05,write_timeout=1)
                candidate.dtr=False;candidate.rts=False;candidate.port=a.port
                try:
                    candidate.open();port=candidate
                    decoder=codecs.getincrementaldecoder('utf-8')(errors='replace')
                    emit('\nCONSOLE: attached without reset\n')
                except serial.SerialException:
                    candidate.close();time.sleep(.2);continue
            try:
                data=port.read(port.in_waiting or 1)
                if data:emit(decoder.decode(data))
            except serial.SerialException:
                port.close();port=None;emit('\nCONSOLE: USB disconnected\n')
    finally:
        if port is not None:port.close()
