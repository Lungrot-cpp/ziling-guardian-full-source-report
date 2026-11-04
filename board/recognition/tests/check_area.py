"""Check exact area weights, rounding and consecutive complete synthetic frames."""
from pathlib import Path
import os,json,hashlib,subprocess
import numpy as np

R=Path(__file__).resolve().parents[1];O=R/'validation'
x=np.arange(1920,dtype=np.uint32)[None,:];y=np.arange(1080,dtype=np.uint32)[:,None]
rgb=np.stack(np.broadcast_arrays((x*3+y*5)%256,(x^y)%256,(x*17+y*13)%256),axis=-1)
horizontal=rgb.reshape(1080,96,20,3).sum(axis=2)
weighted=np.repeat(horizontal,4,axis=0).reshape(96,45,96,3).sum(axis=1)
expected=(weighted+450)//900
for name,values in [('area_stimulus',rgb),('area_expected',expected)]:
 packed=((values[:,:,0]<<16)|(values[:,:,1]<<8)|values[:,:,2]).reshape(-1)
 (O/(name+'.hex')).write_text(''.join(f'{v:06x}\n'for v in packed))
values=np.arange(229501,dtype=np.int64)+450
assert np.array_equal((values*149131)>>27,values//900)
env=os.environ.copy();env['PATH']=r'C:\Users\jsq\Desktop\FPGA\tmp\tools\ucrt64\bin;'+env['PATH'];env.update(TMP=str(R/'validation'),TEMP=str(R/'validation'),TMPDIR=str(R/'validation'))
binary=O/'area.vvp'
c=subprocess.run(['C:/Users/jsq/Desktop/FPGA/tmp/tools/ucrt64/bin/iverilog.exe','-g2012','-s','tb_rgb_area','-o',str(binary),str(R/'tests/tb_rgb_area.sv'),str(R/'fpga/rtl/rgb_area_downsample.sv')],cwd=R,env=env,capture_output=True,text=True)
assert c.returncode==0,c.stderr
p=subprocess.run(['C:/Users/jsq/Desktop/FPGA/tmp/tools/ucrt64/bin/vvp.exe',str(binary)],cwd=R,env=env,capture_output=True,text=True)
(O/'area-simulation.log').write_text(c.stderr+p.stdout+p.stderr)
assert p.returncode==0 and'AREA_PASS'in p.stdout,p.stdout+p.stderr
result=dict(passed=True,output_pixels=18432,rounding_cases=229501,reference='Exact quarter-row area weights, not nearest-neighbor or blur of the 96 input',sources={name:hashlib.sha256((R/name).read_bytes()).hexdigest()for name in ['fpga/rtl/rgb_area_downsample.sv','tests/tb_rgb_area.sv','tests/check_area.py']})
(O/'area-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
