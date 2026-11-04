"""Run production fusion plus existing fall/tracker regressions on RV32, offline only."""
from pathlib import Path
import struct, subprocess, sys, tempfile
sys.path.append('D:/fpga/Demo/Demo/tinyml-main/model_zoo/yolo_person_detection/.venv_train/Lib/site-packages')
from unicorn import Uc, UcError, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_PC

root = Path(__file__).resolve().parents[1]
prefix = 'D:/toolchain/bin/riscv-none-elf-'
with tempfile.TemporaryDirectory() as folder:
    for case in ('infrared_assist_check', 'event_check', 'visual_weighted_check', 'hdmi_fps_check'):
        elf = Path(folder)/(case+'.elf')
        binary = elf.with_suffix('.bin')
        c_case = case == 'hdmi_fps_check'
        subprocess.run([prefix+('gcc.exe' if c_case else 'g++.exe'), '-march=rv32im', '-mabi=ilp32', '-O2',
            '-msmall-data-limit=0', '-mno-relax', '-ffunction-sections', '-fdata-sections',
            '-nostartfiles', '-Wl,--gc-sections', *([] if c_case else ['-fno-exceptions', '-fno-rtti']),
            '-T', 'tests/sensor_test.ld', 'tests/sensor_start.S', 'tests/'+case+('.c' if c_case else '.cpp'),
            '-lm', '-o', str(elf)], cwd=root, check=True)
        subprocess.run([prefix+'objcopy.exe', '-O', 'binary', str(elf), str(binary)], check=True)
        u = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        u.mem_map(0, 128*1024); u.mem_map(0x10000000, 4096)
        if c_case: u.mem_map(0x20000000, 8192)
        u.mem_write(0, binary.read_bytes())
        try:
            u.emu_start(0, 128*1024, count=10000000)
        except UcError:
            pc = u.reg_read(UC_RISCV_REG_PC)
            assert bytes(u.mem_read(pc, 4)) == b'\x73\x00\x10\x00', hex(pc)
        code = struct.unpack('<I', u.mem_read(0x10000000, 4))[0]
        assert code == 0x600d, (case, code)
        print(case, 'PASS (synthetic tests, not physical recognition evidence)')
