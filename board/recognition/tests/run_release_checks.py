"""Execute packaged C++ logic as RV32; this does not emulate FPGA peripherals."""
from pathlib import Path
import subprocess,struct,json
from unicorn import Uc,UcError,UC_ARCH_RISCV,UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_PC
root=Path(__file__).resolve().parents[1]
tool=Path('D:/toolchain/bin/riscv-none-elf-g++.exe')
for case in ('event_check','visual_weighted_check'):
    target='tests/'+case
    subprocess.run([str(tool),'-march=rv32im','-mabi=ilp32','-O2','-msmall-data-limit=0','-mno-relax',
        '-ffunction-sections','-fdata-sections','-nostartfiles','-Wl,--gc-sections','-fno-exceptions','-fno-rtti',
        '-T','tests/sensor_test.ld','tests/sensor_start.S',target+'.cpp','-lm','-o',target+'.elf'],cwd=root,check=True)
    subprocess.run([str(tool.with_name('riscv-none-elf-objcopy.exe')),'-O','binary',target+'.elf',target+'.bin'],cwd=root,check=True)
    u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,128*1024)
    u.mem_write(0,(root/(target+'.bin')).read_bytes());u.mem_map(0x10000000,4096)
    try:u.emu_start(0,128*1024,count=10000000)
    except UcError:
        pc=u.reg_read(UC_RISCV_REG_PC);assert bytes(u.mem_read(pc,4))==b'\x73\x00\x10\x00',hex(pc)
    code=struct.unpack('<I',u.mem_read(0x10000000,4))[0];assert code==0x600d,(case,code)
    print(case,'PASS')
report=dict(status='passed',runtime='RV32 C++ in Unicorn; synthetic observations only',board_tested=False,
    checks=['full96 RGB lookup and coordinate mapping','posture transition yellow','10s effective stillness',
    'missing observation pauses time','motion resets time','invalid/cold thermal blocks red',
    'valid 60/25/15 weighted confirmation','base tracker/sensor scheduling regression'])
(root/'validation/logic_checks.json').write_text(json.dumps(report,indent=2))
