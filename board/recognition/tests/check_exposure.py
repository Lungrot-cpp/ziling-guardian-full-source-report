"""Check unity analog gain without changing V12 exposure or other camera settings."""
from pathlib import Path
import json,re

project=Path(__file__).resolve().parents[1]
out=project.parent/'validation'
baseline=json.loads((out/'baseline.json').read_text(encoding='utf-8'))
relative='fpga/rtl/cam_i2c_ctrl/i2c/i2c_master_reg_rom.v'
pattern=r"(?m)^\s*ROM\[8'h([0-9a-f]+)\]\s*=\s*\{16'h([0-9a-f]+),8'h([0-9a-f]+),1'b([01])\}"

def words(path):
    return {int(i,16):(int(a,16),int(v,16),int(stop))
            for i,a,v,stop in re.findall(pattern,path.read_text(encoding='utf-8'),re.I)}

before=words(Path(baseline['source'])/relative)
after=words(project/relative)
assert before.keys()==after.keys() and len(after)>150
assert {i for i in before if before[i]!=after[i]}=={0xa1,0xa2}
assert before[0xa1]==(0x3e08,0x80,0) and before[0xa2]==(0x3e09,0x2a,0)
assert after[0xa1]==(0x3e08,0,0) and after[0xa2]==(0x3e09,0x20,0)
regs={a:v for a,v,_ in after.values()}
exposure=((regs[0x3e00]&15)<<12)|(regs[0x3e01]<<4)|(regs[0x3e02]>>4)
assert exposure==2528
result=dict(passed=True,changed_registers=['0x3e08','0x3e09'],
            analog_gain=1.0,exposure_half_lines=exposure,
            other_camera_words_unchanged=True,board_tested=False)
(out/'camera-gain-source-check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('CAMERA_UNITY_GAIN_PASS',json.dumps(result))
