"""Load the paired bit/application to volatile memory and record the startup self-check."""
from pathlib import Path
import hashlib,json,sys,threading,time,zlib
import serial

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import project

sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt=json.loads((ROOT/'software/build-accel/build_receipt.json').read_text())
for name,expected in receipt['inputs'].items():assert sha(ROOT/'software'/name)==expected,name
assert sha(ROOT/'software/build-accel/personInferenceDemo.elf')==receipt['elf_sha256']
assert sha(ROOT/'software/src/model/person_demo.tflite')=='9cca374fde1774c62e835c265f7c3dd5b1cb70efcc27f7e3c89c997ef606ceed'
project.check()

stop=threading.Event()
port=serial.Serial('COM7',460800,timeout=.1)
def capture():
    with (ROOT/'runtime/startup_uart.log').open('wb') as log:
        while not stop.is_set():
            data=port.read(8192)
            if data:log.write(data);log.flush()
reader=threading.Thread(target=capture);reader.start()
try:
    project.load()
    stop.wait(18)
finally:
    stop.set();reader.join();port.close()
text=(ROOT/'runtime/startup_uart.log').read_text(errors='replace')
assert 'RESULT_CHECK=PASS' in text and 'candidate=personal_single96' in text and 'V6_DIAGNOSTICS detection_chain=1' in text,text[-4000:]
rows=[]
for line in text[text.rfind('@V6BOOT'):].split('\n')[:-1]:
    line=line.rstrip('\r')
    if not line.startswith('@V6 '):continue
    _,length,crc,payload=line.split(' ',3)
    assert len(payload.encode())==int(length) and zlib.crc32(payload.encode())==int(crc,16)
    rows.append(json.loads(payload))
assert len(rows)>=5 and rows[-1]['detection_seq']>rows[0]['detection_seq']
assert all(len(row['tracks'])<=1 for row in rows)
for row in rows:
    d=row['detection_chain']
    assert d['displayed']==len(row['tracks'])<=1
    assert 0<=d['tracked']<=d['displayed'] and 0<=d['mapped']<=min(16,d['candidates'])<=d['decoded']
    assert len(d['boxes'])==min(16,d['decoded'])
    assert (d['selected_source']>=0)==bool(d['displayed'])
    if d['displayed']:assert d['selected_source']<d['decoded']
    if not d['snapshot_valid']:assert d['decoded']==d['displayed']==0
assert rows[-1]['detection_chain']['attempt']>rows[0]['detection_chain']['attempt']
(ROOT/'validation/board_packets.json').write_text(json.dumps(rows,indent=2))
report=dict(startup_integer_reference_pass=True,live_packets=len(rows),flash_written=False,
            diagnostic_chain_pass=True,with_box=sum(bool(r['tracks']) for r in rows),elf_sha256=receipt['elf_sha256'],model_sha256=sha(ROOT/'software/src/model/person_demo.tflite'))
(ROOT/'validation/board_load.json').write_text(json.dumps(report,indent=2))
print('PERSONAL_SINGLE_BOARD_LOAD_PASS',json.dumps(report))
