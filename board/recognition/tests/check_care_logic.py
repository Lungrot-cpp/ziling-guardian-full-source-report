"""Run the production room mapping/controller with emulated register inputs, not hardware."""
from pathlib import Path
import subprocess,tempfile,struct
from unicorn import Uc,UC_ARCH_RISCV,UC_MODE_RISCV32,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
root=Path(__file__).resolve().parents[1];sw=root/'software';prefix='D:/toolchain/bin/riscv-none-elf-'
with tempfile.TemporaryDirectory() as folder:
 for case,source,streaming in [('care_logic_check','care_replay.cc',False),('care_controller_check','care_demo.cc',False),('care_controller_check','care_demo.cc',True)]:
  elf=Path(folder)/(case+'.elf');binary=elf.with_suffix('.bin')
  subprocess.run([prefix+'g++.exe','-O2','-march=rv32im','-mabi=ilp32','-msmall-data-limit=0','-mno-relax',
   '-I'+str(sw/'src'),'-nostartfiles','-specs=nosys.specs','-Wl,--gc-sections,-T,'+str(sw/'ddr.ld'),
   str(root/'tests'/(case+'.cpp')),str(sw/'src'/source),str(sw/'sdk/software/standalone/common/start.S'),'-o',str(elf)],check=True)
  subprocess.run([prefix+'objcopy.exe','-O','binary',str(elf),str(binary)],check=True)
  u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,0x2001000);u.mem_write(0x1000,binary.read_bytes());u.mem_map(0xf8100000,65536)
  state={'armed':False,'result':None,'index':0,'opens':0,'running':False}
  def mmio(uc,access,address,size,value,data):
   if access==16:
    fields={0xf8100500:0x43415231,0xf8100524:288,0xf8100520:int(state['armed']),0xf8100600:state['opens'],
     0xf810052c:0x53545231 if streaming else 0,0xf8100504:3 if state['running'] else 0}
    if address in fields:uc.mem_write(address,struct.pack('<I',fields[address]))
   elif address==0xf8100520:
    if value in (0,1):state['armed']=bool(value)
    elif value==2 and state['armed']:state['opens']+=1
   elif address==0xf8100504 and streaming and value==2:state.update(index=0,running=True)
   elif address==0xf8100508:state['index']=value
   elif address==0xf810050c:
    uc.mem_write(0xf8101000+state['index']*4,struct.pack('<I',value))
    if streaming:
     state['index']+=1
     if state['index']==288:state['running']=False
   elif address==0xf8100344:state['result']=value;uc.emu_stop()
  u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,mmio)
  u.emu_start(0x1000,0x2000000,count=100000000)
  assert state['result']==0x600d1234,(case,state['result'])
  print(case,'streaming' if streaming else 'indexed','PASS (emulated inputs, not physical location evidence)')
