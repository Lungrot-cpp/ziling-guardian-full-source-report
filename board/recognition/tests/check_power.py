"""Execute the actual power policy on RV32; no physical devices accessed."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
sys.path.append('D:/fpga/Demo/Demo/tinyml-main/model_zoo/yolo_person_detection/.venv_train/Lib/site-packages')
from unicorn import Uc,UcError,UC_ARCH_RISCV,UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_PC
root=Path(__file__).resolve().parents[1]
out=root.parent/'validation';prefix='D:/toolchain/bin/riscv-none-elf-'
subprocess.run([prefix+'g++.exe','-march=rv32im','-mabi=ilp32','-O2','-msmall-data-limit=0','-mno-relax',
 '-fno-exceptions','-fno-rtti','-nostartfiles','-T','tests/sensor_test.ld','tests/sensor_start.S',
 'tests/power_check.cpp','-specs=nosys.specs','-Wl,--gc-sections,--defsym=end=0x18000','-o',str(out/'power-test.elf')],cwd=root,check=True)
subprocess.run([prefix+'objcopy.exe','-O','binary',str(out/'power-test.elf'),str(out/'power-test.bin')],check=True)
u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,128*1024);u.mem_map(0x10000000,4096)
u.mem_write(0,(out/'power-test.bin').read_bytes())
try:u.emu_start(0,128*1024,count=10000000)
except UcError:
 pc=u.reg_read(UC_RISCV_REG_PC);assert bytes(u.mem_read(pc,4))==b'\x73\x00\x10\x00',hex(pc)
code=struct.unpack('<I',u.mem_read(0x10000000,4))[0];assert code==0x600d,('power case failed',code)
report=dict(passed=True,assertions=27,board_tested=False,physical_power_measured=False,
 sources={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ('software/src/power_policy.h','tests/power_check.cpp')})
(out/'power-tests.json').write_text(json.dumps(report,indent=2))
print('POWER_RV32_PASS: 27 assertions; 30s entry, 2s fallback, motion/person/fault wake, alarm guard, mode switch, time wrap, counters and JSON')
