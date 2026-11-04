"""Execute the production voice driver as RV32 with timed UART register behavior."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys
sys.path.append('D:/fpga/Demo/Demo/tinyml-main/model_zoo/yolo_person_detection/.venv_train/Lib/site-packages')
from unicorn import Uc,UcError,UC_ARCH_RISCV,UC_MODE_RISCV32,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.riscv_const import UC_RISCV_REG_PC,UC_RISCV_REG_MSTATUS

root=Path(__file__).resolve().parents[1]
out=root.parent/'validation';out.mkdir(exist_ok=True)
prefix='D:/toolchain/bin/riscv-none-elf-'
subprocess.run([prefix+'g++.exe','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-O2',
 '-msmall-data-limit=0','-mno-relax','-fno-exceptions','-fno-rtti','-nostartfiles',
 '-Isoftware/sdk/bsp/efinix/EfxSapphireSoc/include','-Isoftware/sdk/software/standalone/driver',
 '-Isoftware/sdk/software/standalone/include',
 '-T','tests/sensor_test.ld','tests/sensor_start.S','tests/voice_check.cpp',
 '-o',str(out/'voice-test.elf')],cwd=root,check=True)
subprocess.run([prefix+'objcopy.exe','-O','binary',str(out/'voice-test.elf'),str(out/'voice-test.bin')],check=True)

header=(root/'software/src/voice_messages.h').read_text()
frames={}
for name,body in re.findall(r'static const uint8_t (\w+)\[\]=\{([^}]+)\}',header):
 frame=bytes(int(value,16) for value in body.split(','))
 assert len(frame)<=128,'fixed firmware frame exceeds the shared UART FIFO'
 checksum=0
 for value in frame:checksum^=value
 assert frame[0]==0xfd and int.from_bytes(frame[1:3],'big')==len(frame)-3 and checksum==0
 if frame[3]==1:
  assert frame[4]==1 and len(frame[5:-1])<=200
  decoded=frame[5:-1].decode('gbk')
  assert decoded==dict(voice_boot='[v8][t5]系统已启动。',
   voice_yellow='[v8][t5]检测到疑似跌倒，请注意查看。',
   voice_red='[v10][t5]soundq，检测到异常状态，请立即查看。')[name]
 frames[name]=frame

expected={0:[],1:[(2,'voice_boot'),(4,'voice_yellow'),(6,'voice_red'),(7,'voice_stop')],
 2:[(1,'voice_red'),(5,'voice_red'),(8,'voice_red'),(9,'voice_stop')],
 3:[(4,'voice_yellow')],4:[],5:[(2,'voice_red')],6:[(1,'voice_red')],7:[(1,'voice_red')]}
results=[]
for scenario,wanted in expected.items():
 u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,128*1024)
 u.mem_map(0xf8100000,65536);u.mem_map(0xf8010000,4096);u.mem_map(0x10000000,4096)
 u.mem_write(0,(out/'voice-test.bin').read_bytes());u.mem_write(0x10000000,struct.pack('<I',scenario))
 state=dict(us=0xfffff000 if scenario==5 else 0,playing=0,marker=0,sent=[],
  selected=0,divider=26,queue=[],active=None,wire=[],starts={},durations=[])
 def advance(delta):
  end=state['us']+delta
  while state['active'] and state['active'][0]<=end:
   when,byte,channel=state['active'];state['wire'].append((channel,byte))
   if state['queue']:
    next_byte=state['queue'].pop(0)
    state['active']=(when+(1042 if state['divider']==1301 else 22),next_byte,state['selected'])
   else:state['active']=None
  state['us']=end
 def read(uc,access,address,size,value,data):
  advance(10)
  result=None
  if address==0xf8100468:result=0x56544331 if scenario==0 else 0x56544332
  if address==0xf8100308:result=state['us']&0xffffffff
  if address==0xf8100460:result=state['selected']|(2 if state['playing'] else 0)
  if address==0xf8010004:
   result=((128-len(state['queue']))<<16)|(0x8000 if state['queue'] else 0)
   if scenario==4 or (scenario==6 and state['selected']):result|=0x8000
  if result is not None:uc.mem_write(address,struct.pack('<I',result))
 def write(uc,access,address,size,value,data):
  advance(1)
  if address==0x10000004:
   if state['marker'] in state['starts']:
    state['durations'].append(state['us']-state['starts'][state['marker']])
   state['marker']=value
  if address==0x10000008:state['playing']=value
  if address==0xf8100464:
   assert value in (0,1)
   if scenario!=6:assert not state['active'] and not state['queue'],'routing cut an active character'
   assert not (uc.reg_read(UC_RISCV_REG_MSTATUS)&8),'IRQ writer could interleave'
   state['selected']=value
  if address==0xf8010008:
   assert value in (26,1301)
   if scenario!=6:assert not state['active'] and not state['queue'],'baud changed mid-character'
   state['divider']=value
  if address==0xf801000c:assert value==7
  if address==0xf8010000:
   assert len(state['queue'])<128,'FIFO overwritten'
   if state['selected']:
    assert state['divider']==1301 and not (uc.reg_read(UC_RISCV_REG_MSTATUS)&8)
    state['sent'].append((state['marker'],value))
    state['starts'].setdefault(state['marker'],state['us'])
   else:assert state['divider']==26
   if state['active']:state['queue'].append(value)
   else:state['active']=(state['us']+(1042 if state['divider']==1301 else 22),value,state['selected'])
 u.hook_add(UC_HOOK_MEM_READ,read);u.hook_add(UC_HOOK_MEM_WRITE,write)
 try:u.emu_start(0,128*1024,count=5000000)
 except UcError:
  pc=u.reg_read(UC_RISCV_REG_PC);assert bytes(u.mem_read(pc,4))==b'\x73\x00\x10\x00',hex(pc)
 assert struct.unpack('<I',u.mem_read(0x10000000,4))[0]==0x600d,scenario
 if state['marker'] in state['starts']:
  state['durations'].append(state['us']-state['starts'][state['marker']])
 actual={}
 for marker,byte in state['sent']:actual.setdefault(marker,bytearray()).append(byte)
 assert actual=={marker:frames[name] for marker,name in wanted},(scenario,actual)
 if scenario not in (4,6):assert all(duration<65000 for duration in state['durations']),state['durations']
 if scenario in (4,6):assert 100000<=state['us']<102500,state['us']
 assert state['selected']==0 and state['divider']==26,'logger not restored'
 advance(10000)
 debug_expected=b'L'+(bytes(range(128)) if scenario==7 else b'')+b'R'
 assert bytes(byte for channel,byte in state['wire'] if channel==0)==debug_expected,'debug log corrupted'
 assert bytes(byte for channel,byte in state['wire'] if channel==1)==b''.join(frames[name] for _,name in wanted)
 results.append(dict(scenario=scenario,frames=[name for _,name in wanted],
  modeled_send_us=state['durations'],passed=True))

report=dict(passed=True,board_tested=False,runtime='production RV32 voice.cc in Unicorn',
 cases=results,checks=['GBK text and complete frame XOR/length','old FPGA safely disabled',
 'startup once; yellow deduplicated','red preempts busy yellow; normal cancels',
 'completion + 5s red repeat; no-BUSY fallback 15s','deferred yellow cancelled',
 'stuck TX disables within 100ms and restores logger','32-bit time wrap','no UART overwrite',
 'FIFO and final shifter drained before route/baud changes','debug bytes retained; voice bytes isolated',
 'interrupt mask restored for enabled/disabled callers','128 queued debug bytes fully preserved'],
 source_sha256={name:hashlib.sha256((root/'software/src'/name).read_bytes()).hexdigest()
  for name in ('voice.h','voice.cc','voice_messages.h')})
(out/'voice-tests.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('VOICE_RV32_PASS',json.dumps(results))
