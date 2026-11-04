"""Simulate the routing with the unchanged production Sapphire UART modules."""
from pathlib import Path
import os,re,subprocess
root=Path(__file__).resolve().parents[1];out=root.parent/'validation'
source=(root/'fpga/ip/sapphire_soc/sapphire_soc.v').read_text(encoding='utf-8')
modules={m[1]:m[0] for m in re.finditer(r'(?m)^module\s+(\w+)\b.*?^endmodule',source,re.S)}
needed={};pending=['BmbUartCtrl_ba6762f3781649428952cf46a6343376']
while pending:
    name=pending.pop()
    if name in needed:continue
    needed[name]=modules[name]
    pending += [n for n in re.findall(r'\b\w+\b',modules[name]) if n in modules and n not in needed]
# Other legacy SoC modules use SystemVerilog reserved names; extract only the
# exact UART dependency closure, without changing any of its Verilog statements.
extracted=out/'sapphire_uart.v';extracted.write_text('\n\n'.join(needed.values()),encoding='utf-8')
files=['tests/tb_voice_uart.sv','fpga/rtl/sensors/voice_uart_mux.sv',str(extracted),
       'fpga/rtl/sensors/sensor_apb.sv','fpga/rtl/sensors/sensor_acquire.sv',
       'fpga/rtl/sensors/sensor_i2c.sv','fpga/rtl/sensors/thermal_filter.sv']
env=os.environ.copy();env['PATH']='D:/fpga/tools/ucrt64/bin;'+env.get('PATH','')
subprocess.run(['D:/fpga/tools/ucrt64/bin/iverilog.exe','-g2012','-s','tb_voice_uart',
                '-o',str(out/'voice_uart.vvp'),*files],cwd=root,env=env,check=True)
p=subprocess.run(['D:/fpga/tools/ucrt64/bin/vvp.exe',str(out/'voice_uart.vvp')],
                 cwd=root,env=env,capture_output=True,text=True,check=True)
assert 'VOICE_UART_PASS' in p.stdout
(out/'voice-rtl.log').write_text(p.stdout,encoding='utf-8')
print(p.stdout.strip());print('Production UART modules checked:',len(needed))
