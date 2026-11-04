"""Authorized local diagnostic capture; no JTAG, reset or Flash writes."""
from pathlib import Path
import json,sys,threading,time,urllib.request,urllib.error,zlib
import cv2
import numpy as np
import serial

R=Path(__file__).resolve().parents[1]
out=R/'validation'/sys.argv[1]
assert out.resolve().parent==(R/'validation').resolve() and not out.exists()
out.mkdir()
stop=threading.Event()
def read_uart():
 with serial.Serial('COM7',460800,timeout=.1)as port,(out/'uart.log').open('wb')as log:
  while not stop.is_set():
   data=port.read(8192)
   if data:log.write(data);log.flush()
thread=threading.Thread(target=read_uart);thread.start()
samples=[];errors=[]
try:
 for i in range(18):
  try:
   with urllib.request.urlopen('http://192.168.0.2/api/input.bin',timeout=4)as f:
    data=f.read();frame=int(f.headers['X-Frame-Id']);capture=int(f.headers['X-Capture-Us']);expected=int(f.headers['X-Input-Hash'])
   assert len(data)==55296,len(data)
   rgb=np.frombuffer(data[:27648],np.uint8).reshape(96,96,3)
   value=2166136261
   for byte in data[:27648]:value=((value^byte)*16777619)&0xffffffff
   assert value==expected,(value,expected)
   assert data[27648:]==bytes(byte^128 for byte in data[:27648]),'quantized tensor differs'
   (out/f'input_{i:02}.bin').write_bytes(data)
   cv2.imwrite(str(out/f'input_{i:02}.png'),cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR))
   samples.append(dict(index=i,frame=frame,capture_us=capture,input_hash=expected))
   with urllib.request.urlopen('http://192.168.0.2/frame.bmp',timeout=4)as f:
    preview=f.read();preview_id=int(f.headers['X-Frame-Id'])
   assert preview[:2]==b'BM' and len(preview)==115266
   (out/f'preview_{i:02}.bmp').write_bytes(preview);samples[-1]['preview_frame']=preview_id
  except (OSError,AssertionError,ValueError)as e:errors.append(dict(index=i,error=str(e)))
  stop.wait(.5)
 for attempt in range(3):
  try:
   with urllib.request.urlopen('http://192.168.0.2/api/bayer.bin',timeout=8)as f:
    raw=f.read();frame=int(f.headers['X-Frame-Id'])
   assert len(raw)==1920*1080
   (out/'bayer.bin').write_bytes(raw)
   (out/'bayer.json').write_text(json.dumps(dict(frame=frame,bytes=len(raw),layout='CPU DDR byte order, reverse eight 16-bit lanes per 128-bit beat before reconstructing raster',same_frame_as_rgb=False),indent=2))
   break
  except (OSError,AssertionError,ValueError)as e:errors.append(dict(bayer_attempt=attempt,error=str(e)))
 stop.wait(2)
finally:
 stop.set();thread.join()
packets=[];bad=0
for line in(out/'uart.log').read_bytes().splitlines():
 if not line.startswith(b'@V6 '):continue
 try:
  _,length,crc,payload=line.rstrip(b'\r').split(b' ',3)
  assert len(payload)==int(length) and zlib.crc32(payload)==int(crc,16)
  packets.append(json.loads(payload))
 except (ValueError,AssertionError):bad+=1
by_id={(p['frame'],p['capture_us'],p['detection_chain']['input_hash']):p for p in packets}
for s in samples:
 p=by_id.get((s['frame'],s['capture_us'],s['input_hash']))
 s['packet']=p
(out/'samples.json').write_text(json.dumps(samples,indent=2))
(out/'telemetry.json').write_text(json.dumps(packets,indent=2))
result=dict(samples=len(samples),matched=sum(s['packet']is not None for s in samples),packets=len(packets),bad_packets=bad,errors=errors,flash_written=False,downloaded=False)
(out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
assert samples and result['matched']>0,'No complete frame/hash match; see capture files'
