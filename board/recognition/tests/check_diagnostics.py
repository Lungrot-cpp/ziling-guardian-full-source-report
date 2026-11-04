"""Compile actual telemetry and diagnostic helper for RV32; check its real wire packet."""
from pathlib import Path
import hashlib, json, struct, subprocess, sys, zlib
sys.path.append('D:/fpga/Demo/Demo/tinyml-main/model_zoo/yolo_person_detection/.venv_train/Lib/site-packages')
from unicorn import Uc, UcError, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_PC

root=Path(__file__).resolve().parents[1]
out=root/'validation/diagnostic_tests';out.mkdir(exist_ok=True)
stub=out/'bsp.h'
stub.write_text('#pragma once\n#define BSP_UART_TERMINAL 0\ninline unsigned uart_writeAvailability(unsigned){return 0;}\ninline void uart_write(unsigned,char){}\n')
(out/'test.ld').write_text((root/'tests/sensor_test.ld').read_text().replace('128K','1M'))
tool=Path('D:/toolchain/bin/riscv-none-elf-g++.exe')
args=[str(tool),'-march=rv32im','-mabi=ilp32','-O2','-msmall-data-limit=0','-mno-relax',
      '-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-nostartfiles',
      '-I'+str(out),'-I'+str(root/'software/src'),'-T',str(out/'test.ld'),
      str(root/'tests/sensor_start.S'),str(root/'tests/diagnostic_telemetry.cpp'),
      '-specs=nosys.specs','-Wl,--gc-sections','-lm','-o',str(out/'test.elf')]
subprocess.run(args,check=True)
subprocess.run([str(tool.with_name('riscv-none-elf-objcopy.exe')),'-O','binary',str(out/'test.elf'),str(out/'test.bin')],check=True)
code=(out/'test.bin').read_bytes();results=[]
for mode in range(8):
    u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,1024*1024);u.mem_map(0x10000000,4096)
    u.mem_write(0,code);u.mem_write(0x10000000,struct.pack('<4I',mode,0,0,0))
    try:u.emu_start(0,1024*1024,count=10000000)
    except UcError:
        pc=u.reg_read(UC_RISCV_REG_PC)
        assert bytes(u.mem_read(pc,4))==b'\x73\x00\x10\x00',hex(pc)
    _,status,address,length=struct.unpack('<4I',u.mem_read(0x10000000,16));assert status==0x600d
    packet=bytes(u.mem_read(address,length));(out/f'packet_{mode}.log').write_bytes(packet)
    _,size,crc,payload=packet.rstrip(b'\n').split(b' ',3)
    assert len(payload)==int(size) and zlib.crc32(payload)==int(crc,16)
    row=json.loads(payload);assert row['frame']==30 and row['capture_us']==1000000
    if mode==6:assert 'detection_chain' not in row
    else:
        d=row['detection_chain'];assert d['attempt']==11 and d['selected_source'] in (-1,0)
        assert d['displayed']==len(row['tracks']) and d['input_hash']==0xfedcba98
        if mode==0:assert d['snapshot_valid']==0 and d['invalid_snapshots']==3 and not d['boxes']
        if mode==1:assert d['snapshot_valid']==1 and d['decoded']==0
        if mode==2:
            assert d['decoded']==2 and d['selected_source']==0 and d['tracked']==1
            assert d['boxes'][0]['box_bp']==[1000,2000,3000,9000]
            assert d['boxes'][0]['objectness']==900 and d['boxes'][0]['score']==500
        if mode==3:assert d['mapped']==2 and d['selected_source']==-1 and d['displayed']==0
        if mode==4:assert d['decoded']==17 and len(d['boxes'])==16
        if mode==5:
            b=d['boxes'][0];assert b['box_bp'][:2]==[-2147483647]*2
            assert b['objectness']==-2147483647 and b['box_bp'][2]==1000000
    results.append(dict(mode=mode,packet_bytes=length,crc=True,json=True))
report=dict(checks=results,passed=True,board_used=False,model_or_rules_changed=False,
            sources={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in
                     ('software/src/telemetry.cc','software/src/telemetry.h','software/src/detection_diagnostics.h','tests/diagnostic_telemetry.cpp')})
(out/'result.json').write_text(json.dumps(report,indent=2))
print('DIAGNOSTIC_RV32_CHECK_PASS',json.dumps(results))
