"""Rebuild the existing ROM loader without touching the FPGA or Flash."""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parent
sdk=root.parent/'recognition/software/sdk'
out=root/'build'
out.mkdir(exist_ok=True)
prefix=str(Path(os.environ.get('RISCV_BIN','D:/toolchain/bin'))/'riscv-none-elf-')
subprocess.run([prefix+'gcc.exe','-Os','-march=rv32im_zicsr_zifencei','-mabi=ilp32',
               '-DUSE_GP','-DNO_LIBC_INIT_ARRAY','-ffreestanding','-fno-builtin',
               '-ffunction-sections','-fdata-sections','-nostdlib','-nostartfiles',
               '-I'+str(sdk/'software/standalone/driver'),
               '-I'+str(sdk/'bsp/efinix/EfxSapphireSoc/include'),str(root/'main.c'),
               str(sdk/'software/standalone/common/start.S'),
               '-Wl,--gc-sections,-T,'+str(sdk/'bsp/efinix/EfxSapphireSoc/linker/bootloader.ld'),
               '-o',str(out/'boot.elf')],check=True)
subprocess.run([prefix+'objcopy.exe','-O','binary',str(out/'boot.elf'),str(out/'boot.bin')],check=True)
data=(out/'boot.bin').read_bytes().ljust(4096,b'\0')
for lane in range(4):
    expected=bytes(int(v,2) for v in (root.parent/'recognition/fpga/ip/sapphire_soc'/
                   f'EfxSapphireSoc.v_toplevel_system_ramA_logic_ram_symbol{lane}.bin').read_text().split())
    assert data[lane::4]==expected,'Rebuilt loader differs from the current FPGA ROM'
print('BOOT_ROM_BUILD_MATCH')
