"""Continuous COM5 logger. Lines appended to cmd file are sent; '!reset' pulses RTS."""
import sys, time, serial, os

port_name, log_path, cmd_path = sys.argv[1], sys.argv[2], sys.argv[3]
open(cmd_path, 'a').close()
cmd_pos = os.path.getsize(cmd_path)
log = open(log_path, 'a', encoding='utf-8', buffering=1)
buf = b''

def stamp(text):
    log.write('[%s] %s\n' % (time.strftime('%H:%M:%S'), text))

while True:
    try:
        port = serial.Serial(port=None, baudrate=1000000, timeout=0.05, write_timeout=1)
        port.dtr = False
        port.rts = False
        port.port = port_name
        port.open()
        stamp('== opened %s' % port_name)
        while True:
            data = port.read(port.in_waiting or 1)
            if data:
                buf += data
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    stamp(line.decode('utf-8', 'replace').rstrip('\r'))
            size = os.path.getsize(cmd_path)
            if size > cmd_pos:
                with open(cmd_path, 'rb') as f:
                    f.seek(cmd_pos)
                    new = f.read().decode('utf-8', 'replace')
                cmd_pos = size
                for cmd in new.splitlines():
                    stamp('>> ' + cmd)
                    if cmd == '!reset':
                        port.rts = True
                        time.sleep(0.3)
                        port.rts = False
                    else:
                        port.write((cmd + '\r\n').encode())
    except (serial.SerialException, OSError) as error:
        stamp('== port error: %s' % error)
        time.sleep(1)
