"""Simulate production HDMI dimming; no board or physical power measurement."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

root = Path(__file__).resolve().parents[1]
out = root.parent / 'validation'
env = os.environ.copy()
env['PATH'] = 'D:/fpga/tools/ucrt64/bin;' + env['PATH']
sources = ['tests/tb_power_display.sv', 'fpga/rtl/person_video_bridge.sv', 'fpga/rtl/rgb_area_downsample.sv']
subprocess.run(['D:/fpga/tools/ucrt64/bin/iverilog.exe', '-g2012', '-s', 'tb_power_display',
                '-o', str(out/'power-display.vvp'), *sources], cwd=root, env=env, check=True)
result = subprocess.run(['D:/fpga/tools/ucrt64/bin/vvp.exe', str(out/'power-display.vvp')],
                        cwd=root, env=env, capture_output=True, text=True, check=True)
assert 'POWER_DISPLAY_RTL_PASS' in result.stdout
(out/'power-display-rtl.log').write_text(result.stdout, encoding='utf-8')
(out/'power-display-rtl.json').write_text(json.dumps(dict(passed=True, board_tested=False,
    hdmi_sync_unchanged=True, brightness_quarter=True, frame_boundary_switch=True,
    raw_detector_input_unchanged=True, wake_and_reset_restore=True,
    sources={name: hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sources}), indent=2))
print(result.stdout.strip())
