"""Run the production RV32 lwIP/HTTP server against real TCP packets in Unicorn."""
from pathlib import Path
import struct,subprocess,tempfile,json,gzip,sys
from unicorn import Uc,UC_ARCH_RISCV,UC_MODE_RISCV32,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.riscv_const import UC_RISCV_REG_PC
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=ROOT/'tests/care_http_check.cpp'
if '--capture' in sys.argv:ROOT=ROOT.parent/'capture'
SW=ROOT/'software';PREFIX='D:/toolchain/bin/riscv-none-elf-'
lwip='sdk/bsp/efinix/EfxSapphireSoc/app/lwip'
objects=list((SW/'build-accel'/lwip).rglob('*.o'))+[SW/'build-accel/src'/name for name in ('onboard_http.cc.o','care_demo.cc.o','care_replay.cc.o','tracking_runtime.cc.o','power_policy.cc.o','event_report.cc.o')]
with tempfile.TemporaryDirectory() as folder:
    elf=Path(folder)/'http.elf';binary=elf.with_suffix('.bin')
    command=[PREFIX+'g++.exe','-O2','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-DUSE_GP','-DNO_LIBC_INIT_ARRAY',
      '-I'+str(SW/'src'),'-I'+str(SW/'src/sensors'),'-nostartfiles','-specs=nosys.specs','-Wl,--gc-sections,-T,'+str(SW/'ddr.ld'),
      str(FIXTURE),str(SW/'sdk/software/standalone/common/start.S'),*map(str,objects),'-o',str(elf)]
    if '--event-report-check' in sys.argv:command.insert(1,'-DEVENT_REPORT_CHECK')
    subprocess.run(command,cwd=SW,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    subprocess.run([PREFIX+'objcopy.exe','-O','binary',str(elf),str(binary)],check=True)
    program=binary.read_bytes()
u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,0x2001000);u.mem_write(0x1000,program)
u.mem_map(0xf8100000,65536);u.mem_map(0xf8b00000,65536);u.mem_map(0xf8010000,4096)
u.mem_write(0xf8010004,struct.pack('<I',255<<16))
slots=[None,b'\0'*60];readslot=0;writeslot=0;out=[];ticks=0
def mmio(uc,access,address,size,value,data):
    global readslot,ticks
    if access==16:
        result=None
        if address==0xf810c000:result=0x4e494331
        elif address==0xf810c004:result=sum((16<<i) for i,p in enumerate(slots) if p is not None)
        elif address==0xf810c00c:result=len(slots[readslot] or b'')
        elif 0xf810d000<=address<0xf810d800:
            p=slots[readslot] or b'';start=address-0xf810d000;result=int.from_bytes(p[start:start+4].ljust(4,b'\0'),'little')
        elif address==0xf8b0bff8:ticks+=1000;result=ticks&0xffffffff
        elif address==0xf8b0bffc:result=ticks>>32
        if result is not None:uc.mem_write(address,struct.pack('<I',result))
    elif address==0xf810c008:readslot=value
    elif address==0xf810c010:slots[readslot]=None
    elif address==0xf810c014:out.append(bytes(uc.mem_read(0xf810c800,value)))
    elif address==0xf8100344:uc.reg_write(UC_RISCV_REG_PC,uc.reg_read(UC_RISCV_REG_PC)+4);uc.emu_stop()
u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,mmio)
def run():u.emu_start(u.reg_read(UC_RISCV_REG_PC) or 0x1000,0x2000000,count=10000000)
run()
assert slots==[None]*2,'CPU reload must drain pending MAC slots'
def inject(packet):
    global writeslot
    assert slots[writeslot] is None
    slots[writeslot]=packet.ljust(60,b'\0');writeslot=(writeslot+1)%2;run()
pcmac=bytes.fromhex('b025aa6c40df');boardmac=bytes.fromhex('000a3501fec0')
pcip=bytes([192,168,0,3]);boardip=bytes([192,168,0,2])
def checksum(data):
    if len(data)%2:data+=b'\0'
    n=sum(struct.unpack('!%dH'%(len(data)//2),data))
    while n>>16:n=(n&65535)+(n>>16)
    return (~n)&65535
arp=struct.pack('!HHBBH',1,0x800,6,4,1)+pcmac+pcip+b'\0'*6+boardip
out.clear();inject(b'\xff'*6+pcmac+b'\x08\x06'+arp)
assert out and out[-1][12:14]==b'\x08\x06' and out[-1][20:22]==b'\x00\x02','ARP reply missing'
port=20000
def request(parts,half_close=False):
    global port
    port+=1;seq=100;ack=0;body=bytearray();closed=False
    def send(flags,payload=b''):
        nonlocal seq
        tcp=struct.pack('!HHIIBBHHH',port,80,seq,ack,5<<4,flags,65535,0,0)+payload
        c=checksum(pcip+boardip+b'\0\x06'+struct.pack('!H',len(tcp))+tcp);tcp=tcp[:16]+struct.pack('!H',c)+tcp[18:]
        ip=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(tcp),1,0x4000,64,6,0,pcip,boardip)
        ip=ip[:10]+struct.pack('!H',checksum(ip))+ip[12:]
        inject(boardmac+pcmac+b'\x08\x00'+ip+tcp)
        seq+=len(payload)+(1 if flags&3 else 0)
    def drain():
        nonlocal ack,closed
        packets=out[:];out.clear()
        for p in packets:
            if p[12:14]!=b'\x08\x00':continue
            ip=p[14:34];total=int.from_bytes(ip[2:4],'big');tcp=p[34:14+total]
            assert checksum(ip)==0 and checksum(boardip+pcip+b'\0\x06'+struct.pack('!H',len(tcp))+tcp)==0
            source,dest,server_seq,_,offset,flags,_,_,_=struct.unpack('!HHIIBBHHH',tcp[:20])
            if dest!=port:continue
            payload=tcp[(offset>>4)*4:]
            if flags&2:ack=server_seq+1
            elif payload:
                if server_seq==ack:body.extend(payload);ack+=len(payload)
                send(0x10)
            if flags&1:ack+=1;send(0x10 if half_close else 0x11);closed=True
    out.clear();send(2);drain();assert ack,'SYN-ACK missing';send(0x10)
    for i,part in enumerate(parts):send(0x19 if half_close and i==len(parts)-1 else 0x18,part);drain()
    for _ in range(1000):
        drain()
        if closed:break
        run()
    assert closed,('HTTP did not finish',port,parts[0][:30],len(body),bytes(body[:160]));headers,data=bytes(body).split(b'\r\n\r\n',1)
    assert len(data)==int(next(line.split(b':',1)[1] for line in headers.split(b'\r\n') if line.startswith(b'Content-Length:')))
    return headers,data
h,data=request([b'GET /api/status HTTP/1.1\r\nHost: board\r\n',b'\r\n']);s=json.loads(data)
assert b'200 OK' in h and s['server']=='riscv-lwip' and s['frame_id']==7 and s['tracks'][0]['id']==1 and s['sensors']['heat']==[2600]*768
h,data=request([b'GET /api/events HTTP/1.1\r\n\r\n']);assert json.loads(data)['events'][0]['state']==2
h,data=request([b'GET /api/infrared HTTP/1.1\r\n\r\n'])
assert json.loads(data)==dict(enabled=1,active=1,calibrated=0)
assert s['tracks'][0]['person_confirmed']==1 and s['infrared_assist']['active']==1
for body,enabled in ((b'enabled=0',0),(b'enabled=1',1)):
 h,data=request([b'POST /api/infrared HTTP/1.1\r\nContent-Length: 9\r\n\r\n',body[:4],body[4:]])
 assert b'200 OK' in h and json.loads(data)['ok']
 h,data=request([b'GET /api/infrared HTTP/1.1\r\n\r\n']);assert json.loads(data)['enabled']==enabled
for body in (b'enabled=2',b'enabled=true',b'enabled=0&enabled=1',b'enabled=0\0x'):
 h,_=request([b'POST /api/infrared HTTP/1.1\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body]);assert b'409' in h
h,data=request([b'GET /api/infrared HTTP/1.1\r\n\r\n']);assert json.loads(data)['enabled']==1
print('INFRARED_HTTP_PASS: default on, fragmented POST both ways, readback, invalid input rejected without mutation')
for body,automatic in ((b'mode=auto',1),(b'mode=continuous',0)):
 h,data=request([b'POST /api/power HTTP/1.1\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n',body])
 assert b'200 OK' in h and json.loads(data)['ok']
 h,data=request([b'GET /api/power HTTP/1.1\r\n\r\n']);power=json.loads(data)
 assert power['automatic']==automatic and power['mode']=='waking' and power['power_watts'] is None
h,_=request([b'POST /api/power HTTP/1.1\r\nContent-Length: 10\r\n\r\nmode=sleep']);assert b'409' in h
h,_=request([b'POST /api/power HTTP/1.1\r\n\r\n']);assert b'411' in h
h,data=request([b'GET /api/care HTTP/1.1\r\n\r\n']);assert json.loads(data)['hardware'] is False
assert json.loads(data)['config']['leds_per_room']==7 and json.loads(data)['wall']==-1 and json.loads(data)['countdown_ms']==0
assert [r['rect'] for r in json.loads(data)['rooms']]==[[0,0,.5,.5],[.5,0,1,.5],[0,.5,.5,1],[.5,.5,1,1]]
h,data=request([b'GET /care.html HTTP/1.1\r\n\r\n']);assert '四点标定'.encode() in data
body=b'p0=0&p1=0&p2=1&p3=0&p4=1&p5=1&p6=0&p7=1'
header=b'POST /api/care/config HTTP/1.1\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'
h,data=request([header,body[:15],body[15:]]);assert b'200 OK' in h and json.loads(data)['ok'],(h,data)
h,data=request([b'GET /api/care HTTP/1.1\r\n\r\n']);assert json.loads(data)['calibrated'] is True
for body in (b'brightness=999',b'p0=0',b'neutral_us=nan',b'r00=0.8'):
 h,_=request([b'POST /api/care/config HTTP/1.1\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body]);assert b'409' in h
body=b'cmd=open'
h,_=request([b'POST /api/care/action HTTP/1.1\r\nContent-Length: 8\r\n\r\n'+body]);assert b'409' in h
h,data=request([b'GET /api/replay HTTP/1.1\r\n\r\n']);assert not json.loads(data)['ready']
h,_=request([b'GET /replay.bmp?clip=1&index=0 HTTP/1.1\r\n\r\n']);assert b'404' in h
h,_=request([b'GET /replay.bmp?clip=1&index=0junk HTTP/1.1\r\n\r\n']);assert b'400' in h
h,_=request([b'POST /api/care/config HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n']);assert b'400' in h
h,_=request([b'POST /api/care/config HTTP/1.1\r\n\r\n']);assert b'411' in h
h,data=request([b'GET /frame.bmp?frame=7 HTTP/1.1\r\n\r\n'])
expected=bytearray(i%251 for i in range(172800))
for y in range(10,151):
    for x in range(10,101):
        if x in (10,100) or y in (10,150):expected[(y*320+x)*3:(y*320+x)*3+3]=bytes([46,57,222])
packed=b''.join(struct.pack('<H',((expected[i+2]>>3)<<11)|((expected[i+1]>>2)<<5)|(expected[i]>>3)) for i in range(0,len(expected),3))
assert data[:2]==b'BM' and len(data)==115266 and data[66:]==packed
h,data=request([b'GET / HTTP/1.1\r\n\r\n']);assert b'200 OK' in h and 'RISC-V'.encode() in data
for path,expected in [('/style.css',b'.sidebar'),('/app.js',b'boardPicture'),('/privacy.js',b'vfPrivacy')]:
    h,data=request([('GET '+path+' HTTP/1.1\r\n\r\n').encode()]);assert b'Content-Encoding: gzip' in h and expected in gzip.decompress(data)
h,data=request([b'GET /media/observer.png HTTP/1.1\r\n\r\n']);assert data.startswith(b'\x89PNG')
h,data=request([b'GET /frame.bmp HTTP/1.1\r\n\r\n'],half_close=True);assert len(data)==115266
for payload,code in [(b'GET /missing HTTP/1.1\r\n\r\n',b'404'),(b'POST / HTTP/1.1\r\n\r\n',b'405'),(b'GET / HTTP/1.1\r\nX:'+b'a'*1010,b'431')]:
    h,_=request([payload]);assert code in h
print('ONBOARD_HTTP_TEST_PASS: actual RV32 lwIP ARP/TCP checksums, fragmented GET, JSON sensors/events, complete 320x180 BMP, half-close, page, 404/405/431, connection reuse')
if '--capture' not in sys.argv:
 h,data=request([b'GET /api/status HTTP/1.1\r\n\r\n']);before=json.loads(data)
 assert before['monitor_reset']==dict(requested=0,completed=0) and before['alarm_latched']==1
 for payload,code in [(b'GET /api/monitor/reset HTTP/1.1\r\n\r\n',b'404'),
   (b'POST /api/monitor/reset HTTP/1.1\r\n\r\n',b'411'),
   (b'POST /api/monitor/reset HTTP/1.1\r\nContent-Length: 1\r\n\r\nx',b'409')]:
  h,_=request([payload]);assert code in h
 h,data=request([b'GET /api/status HTTP/1.1\r\n\r\n']);assert json.loads(data)['monitor_reset']==dict(requested=0,completed=0)
 for expected in [1,2]:
  h,data=request([b'POST /api/monitor/reset HTTP/1.1\r\nContent-Length: 0\r\n',b'\r\n'])
  assert b'200 OK' in h and json.loads(data)==dict(ok=True,reset_id=expected)
  h,data=request([b'GET /api/status HTTP/1.1\r\n\r\n']);after=json.loads(data)
  assert after['monitor_reset']==dict(requested=expected,completed=expected) and not after['alarm_latched'] and not after['tracks']
  assert after['infrared_assist']==before['infrared_assist']
  h,data=request([b'GET /api/events HTTP/1.1\r\n\r\n']);assert not json.loads(data)['events']
  if expected==1:
   u.mem_write(0xf810034c,struct.pack('<I',1));run()
   h,data=request([b'GET /api/events HTTP/1.1\r\n\r\n']);second=json.loads(data)['events']
   assert second[0]['id']>1 and second[0]['state']==2
   h,data=request([b'GET /api/status HTTP/1.1\r\n\r\n']);assert json.loads(data)['alarm_latched']==1
 print('MONITOR_RESET_HTTP_PASS: invalid requests do not reset, fragmented POST, completed readback, repeated reset, second fall preserves unique event IDs and infrared mode; fixture only')
print('CARE_HTTP_TEST_PASS: production RV32 server; fragmented POST config; calibration; invalid/partial config rejection; disarmed door rejection; replay metadata and unavailable/malformed frame requests')
if '--capture' in sys.argv:
 h,data=request([b'GET /api/collect/info HTTP/1.1\r\n\r\n']);info=json.loads(data)
 assert info['width']==96 and info['height']==96
 assert info['expected_bit_sha256']==json.loads((ROOT/'fpga/outflow/build_receipt.json').read_text())['bit_sha256']
 h,_=request([b'GET /api/collect.bin HTTP/1.1\r\n\r\n']);assert b'503' in h
 print('CAPTURE_CARE_HTTP_PASS: original collect routes and new care routes coexist')
