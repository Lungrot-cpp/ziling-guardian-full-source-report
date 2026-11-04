"""Run production (non-SENSOR_TEST) RV32 driver against the hardware register contract."""
from pathlib import Path
import struct, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
from unicorn import Uc, UcError, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.riscv_const import UC_RISCV_REG_PC
tool=Path('D:/toolchain/bin/riscv-none-elf-gcc.exe')
subprocess.run([str(tool),'-march=rv32im','-mabi=ilp32','-O2','-msmall-data-limit=0','-mno-relax',
 '-fno-builtin','-ffunction-sections','-fdata-sections','-nostartfiles','-Wl,--gc-sections',
 '-Isoftware/sdk/bsp/efinix/EfxSapphireSoc/include','-Isoftware/src/sensors/vendor','-T','tests/sensor_test.ld','tests/sensor_start.S',
 'tests/hardware_driver_check.c','software/src/sensors/vendor/MLX90640_API.c','-lm','-lc','-lgcc',
 '-o','tests/hardware-driver.elf'],cwd=ROOT,check=True)
subprocess.run([str(tool.with_name('riscv-none-elf-objcopy.exe')),'-O','binary',
 'tests/hardware-driver.elf','tests/hardware-driver.bin'],cwd=ROOT,check=True)
u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,128*1024)
u.mem_write(0,(ROOT/'tests/hardware-driver.bin').read_bytes());u.mem_map(0xf8100000,65536)
index=0;stage=0;filter_value=0;filter_writes=0
def read(uc,access,address,size,value,data):
 global filter_writes
 result=None
 if address==0xf8100308:result=1000000
 reg=(address-0xf8100400)//4
 if address>=0xf8100400:
  result={3:2 if stage==0 else (4 if index<600 else 5),4:2,5:800000,6:900000,
          7:0x1000,8:0x1000,10:1,11:900000,12:0x938000a2,13:0x6666,21:filter_value}.get(reg,0)
  if reg==2:result=100 if index%832 in (778,800) else 0
 if result is not None:uc.mem_write(address,struct.pack('<I',result))
def write(uc,access,address,size,value,data):
 global index,stage,filter_value,filter_writes
 if address==0xf8100404:index=value
 if address==0xf8100454:filter_value=value&65535;filter_writes+=1
 if address==0xf8100340:stage=value
u.hook_add(UC_HOOK_MEM_READ,read);u.hook_add(UC_HOOK_MEM_WRITE,write)
try:u.emu_start(0,128*1024,count=50000000)
except UcError:
 pc=u.reg_read(UC_RISCV_REG_PC)
 assert bytes(u.mem_read(pc,4))==b'\x73\x00\x10\x00',hex(pc)
status=struct.unpack('<I',u.mem_read(0xf8100344,4))[0]
assert status==0x600d,('Hardware driver assertion',status)
assert filter_writes==768,filter_writes
print('HARDWARE_DRIVER_TEST_PASS: production RV32 hardware path, CRC, two subpages, FPGA filter interface, seqlock rejection, causal/stale selection')
