"""Compare both color phases; this is not an end-to-end camera acceptance test."""
from pathlib import Path
import json,os,subprocess

R=Path(__file__).resolve().parents[1]
B=R.parent/'personal_single_daylight_20261025'
V=R.parent/'personal_single_v9_imagefix_20261025'
template=(V/'tests/tb_debayer.sv').read_text()
template=template.replace("if(rst && {dut.w_debayer_ah","if(1'b0 && rst && {dut.w_debayer_ah")
env=os.environ.copy();env.update(TMP='D:/fpga/tmp',TEMP='D:/fpga/tmp',TMPDIR='D:/fpga/tmp')
results=[]
for core,root in [('old',B),('v9',V)]:
 for phase,pattern in [('BGGR',"data=y%2 ? {8'd120,8'd80} : {8'd80,8'd40};"),('GBRG',"data=y%2 ? {8'd80,8'd120} : {8'd40,8'd80};")]:
  tb=R/'validation'/f'phase_{core}_{phase}.sv'
  tb.write_text(template.replace("data=y%2 ? {8'd120,8'd80} : {8'd80,8'd40};",pattern))
  out=tb.with_suffix('.vvp')
  sources=[root/'fpga/rtl/debayer'/n for n in ['debayer_top_2to1.v','raw_to_rgb.v','rgb_gain_v1.v','line_buffer.v','true_dual_port_ram.v']]
  c=subprocess.run(['D:/fpga/tools/ucrt64/bin/iverilog.exe','-g2012','-s','tb_debayer','-o',str(out),str(tb),*map(str,sources)],env=env,capture_output=True,text=True)
  assert c.returncode==0,c.stderr
  p=subprocess.run(['D:/fpga/tools/ucrt64/bin/vvp.exe',str(out)],env=env,capture_output=True,text=True)
  results.append(dict(core=core,phase=phase,returncode=p.returncode,log=p.stdout+p.stderr))
assert results[2]['returncode']==0
(R/'validation/phase-isolation.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
