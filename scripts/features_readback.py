"""Bounded real-watch feature readback; never create or finish a ride/audio memo."""
import argparse
import asyncio
import codecs
import json
from pathlib import Path
import time
from bleak import BleakClient, BleakScanner

parser=argparse.ArgumentParser()
parser.add_argument('--before',type=Path,required=True)
parser.add_argument('--log',type=Path,required=True)
args=parser.parse_args()
events=[]
report={'passed':False,'events':events,'error':None,'scope':'Real BLE protocol only; temporary synthetic navigation/GPS are cleared; no saved ride/audio writes.'}
baseline=next(e['reply'] for e in json.loads(args.before.read_text(encoding='utf-8')) if e.get('command')=='ride_latest')
SERVICE='0000fff0-0000-1000-8000-00805f9b34fb'
TX='0000fff1-0000-1000-8000-00805f9b34fb'
RX='0000fff2-0000-1000-8000-00805f9b34fb'

async def main():
    device=await BleakScanner.find_device_by_filter(lambda d,a:a.local_name=='VelaRide',timeout=20)
    if device is None:raise TimeoutError('VelaRide not found')
    async with BleakClient(device,timeout=20,services=[SERVICE],winrt={'use_cached_services':False}) as client:
        decoder=codecs.getincrementaldecoder('utf-8')()
        pending=''
        queue=asyncio.Queue()
        def receive(sender,data):
            nonlocal pending
            pending+=decoder.decode(data)
            if len(pending)>4096:raise ValueError('Oversized response')
            while '\n' in pending:
                line,pending=pending.split('\n',1)
                if line.strip():queue.put_nowait(json.loads(line))
        await client.start_notify(TX,receive)
        async def send(cmd,event=None,ok=True,error=None,**fields):
            frame=(json.dumps({'cmd':cmd,**fields},ensure_ascii=False)+'\n').encode()
            for start in range(0,len(frame),18):
                await asyncio.wait_for(client.write_gatt_char(RX,frame[start:start+18],response=True),8)
            reply=await asyncio.wait_for(queue.get(),12)
            events.append({'command':cmd,'fields':fields,'reply':reply})
            print(json.dumps(events[-1],ensure_ascii=False),flush=True)
            if ok is not None:assert reply.get('ok') is ok,reply
            if reply.get('ok'):assert reply.get('event')==(event or cmd),reply
            if error is not None:assert reply.get('error')==error,reply
            return reply
        temporary_touched=False
        try:
            await send('ping','pong')
            state=await send('ride_status')
            assert not state['active'],'Do not inject temporary data into an active ride'
            initial=await send('ride_latest')
            for key,value in baseline.items():assert initial.get(key)==value,(key,value,initial.get(key))
            assert initial['safety_version']==0 and initial['extras_version']==0
            index=await send('ride_history')
            assert index['capacity']==16 and index['corrupt_count']==0,index
            assert initial['sequence'] in index['sequences']
            one=await send('ride_get','ride_record',sequence=initial['sequence'])
            assert {k:v for k,v in one.items() if k!='event'}=={k:v for k,v in initial.items() if k!='event'}
            await send('ride_get',ok=False,error='bad_sequence',sequence=-1)
            missing=initial['sequence']+1000
            await send('ride_get',ok=False,error='history_changed',sequence=missing)
            temporary_touched=True
            await send('nav_hint',direction=1,distance_m=80,offroute=0)
            await send('nav_hint',ok=False,error='bad_nav_hint',direction=6,distance_m=80,offroute=0)
            await send('nav_clear')
            await send('phone_data',speed=12.5,distance=1.25)
            state=await send('ride_status')
            assert state['phone_valid'] and state['speed_kmh']==12.5 and state['distance_km']==1.25
            await send('phone_clear')
            state=await send('ride_status')
            assert not state['phone_valid'] and state['ride_distance_m']==0
            memo=await send('memo_list',ok=None)
            if memo.get('ok'):
                assert isinstance(memo.get('ids'),list) and len(memo['ids'])<=16
                report['tf_mounted']=True
            else:
                assert memo.get('error')=='memo_storage_unavailable'
                report['tf_mounted']=False
            await send('memo_info',ok=False,error='bad_memo_id',id=0)
            await send('memo_read',ok=False,error='bad_memo_range',id=1,offset=0,length=161)
            await send('time_sync',epoch=int(time.time()),timezone_min=480)
            final=await send('ride_latest')
            assert final==initial,'Saved user ride changed during protocol checks'
            report['preserved_record']=final
            report['history_index']=index
            report['passed']=True
        finally:
            try:
                if temporary_touched:
                    await send('nav_clear')
                    await send('phone_clear')
            finally:
                await client.stop_notify(TX)

try:
    asyncio.run(main())
except Exception as error:
    report['passed']=False
    report['error']=f'{type(error).__name__}: {error}'
    print(report['error'],flush=True)
finally:
    args.log.parent.mkdir(parents=True,exist_ok=True)
    args.log.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
raise SystemExit(0 if report['passed'] and report['error'] is None else 1)
