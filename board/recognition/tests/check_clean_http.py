"""Verify temporary exports are gone and the normal board services still update."""
from pathlib import Path
import hashlib,json,time,urllib.request,urllib.error

R=Path(__file__).resolve().parents[1];base='http://192.168.0.2'
removed={}
for path in ['/api/input.bin','/api/bayer.bin']:
 try:
  urllib.request.urlopen(base+path,timeout=4).close()
  raise AssertionError('Temporary export still reachable: '+path)
 except urllib.error.HTTPError as error:
  assert error.code==404,(path,error.code);removed[path]=error.code
statuses=[];frames=[];errors=[]
for attempt in range(8):
 with urllib.request.urlopen(base+'/api/status',timeout=4)as response:status=json.load(response)
 assert status['server']=='riscv-lwip' and len(status['tracks'])<=1
 assert len(status['sensors']['heat'])==768
 statuses.append({k:status[k]for k in ['uptime_ms','frame_id','image_frame','period_ms']})
 try:
  with urllib.request.urlopen(base+'/frame.bmp',timeout=4)as response:
   data=response.read();frame=int(response.headers['X-Frame-Id'])
  assert data[:2]==b'BM' and len(data)==115266
  frames.append(frame);(R/'validation/clean-current.bmp').write_bytes(data)
 except urllib.error.HTTPError as error:
  assert error.code==503,error.code;errors.append(error.code)
 time.sleep(.4)
assert statuses[-1]['uptime_ms']>statuses[0]['uptime_ms']
assert statuses[-1]['frame_id']>statuses[0]['frame_id']
assert len(set(frames))>=2,frames
with urllib.request.urlopen(base+'/',timeout=4)as response:assert b'<html'in response.read().lower()
result=dict(passed=True,temporary_endpoints=removed,statuses=statuses,image_frames=frames,transient_image_unavailable=errors,flash_written=False,bin_sha256=hashlib.sha256((R/'software/build-accel/personInferenceDemo.bin').read_bytes()).hexdigest())
(R/'validation/clean-http.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
