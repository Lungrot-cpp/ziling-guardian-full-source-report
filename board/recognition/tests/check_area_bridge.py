from pathlib import Path
import hashlib,json,os,subprocess
R=Path(__file__).resolve().parents[1]
top=(R/'fpga/rtl/v6_person_top.v').read_bytes()
assert b'.r(hdmi_tx_rdata),.g(hdmi_tx_gdata),.b(hdmi_tx_bdata)'in top
assert b'.r(hdmi_tx_bdata),.g(hdmi_tx_gdata),.b(hdmi_tx_rdata)'not in top
env=os.environ.copy();env['PATH']=r'C:\Users\jsq\Desktop\FPGA\tmp\tools\ucrt64\bin;'+env['PATH'];env.update(TMP=str(R/'validation'),TEMP=str(R/'validation'),TMPDIR=str(R/'validation'))
binary=R/'validation/area_bridge.vvp'
sources=['tests/tb_area_bridge.sv','fpga/rtl/person_video_bridge.sv','fpga/rtl/rgb_area_downsample.sv']
c=subprocess.run(['C:/Users/jsq/Desktop/FPGA/tmp/tools/ucrt64/bin/iverilog.exe','-g2012','-s','tb_area_bridge','-o',str(binary),*[str(R/p)for p in sources]],env=env,cwd=R,capture_output=True,text=True)
assert c.returncode==0,c.stderr
p=subprocess.run(['C:/Users/jsq/Desktop/FPGA/tmp/tools/ucrt64/bin/vvp.exe',str(binary)],env=env,cwd=R,capture_output=True,text=True)
(R/'validation/area-bridge.log').write_text(c.stderr+p.stdout+p.stderr)
assert p.returncode==0 and'AREA_BRIDGE_PASS'in p.stdout,p.stdout+p.stderr
result=dict(passed=True,pixels_read_through_apb=18432,valid_frame=True,changed_tag_rejected=True,rgb_top_wiring=True,sources={name:hashlib.sha256((R/name).read_bytes()).hexdigest()for name in sources+['fpga/rtl/v6_person_top.v','tests/check_area_bridge.py']})
(R/'validation/area-bridge-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
