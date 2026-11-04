"""Read current UART/HTTP only; never download, halt, reset, or write Flash."""
from pathlib import Path
import json, time, urllib.request, zlib, sys
import serial

base = Path(__file__).resolve().parents[1] / 'validation'
out = base / (sys.argv[1] if len(sys.argv)>1 else 'live')
assert out.resolve().parent == base.resolve()
out.mkdir(parents=True,exist_ok=True)
assert not (out / 'uart.log').exists(), 'Keep the previous capture intact'
start = time.monotonic()
previews = []
errors = []
with serial.Serial('COM7', 460800, timeout=.05) as port, (out / 'uart.log').open('wb') as log:
    next_image = start
    while time.monotonic() - start < 35:
        data = port.read(max(1, min(65536, port.in_waiting)))
        if data:
            log.write(data)
        if time.monotonic() >= next_image:
            index = len(previews)
            row = {'elapsed_s': time.monotonic() - start}
            try:
                with urllib.request.urlopen('http://192.168.0.2/frame.bmp', timeout=3) as r:
                    image = r.read()
                    assert image[:2] == b'BM' and len(image) == 115266
                    row.update(image_frame=int(r.headers['X-Frame-Id']),
                               image_capture_ms=int(r.headers['X-Capture-Ms']))
                (out / f'frame_{index:02}.bmp').write_bytes(image)
                with urllib.request.urlopen('http://192.168.0.2/api/status', timeout=3) as r:
                    row['status'] = json.load(r)
                previews.append(row)
            except Exception as e:
                errors.append({'elapsed_s': time.monotonic()-start, 'error': str(e)})
            next_image = time.monotonic() + .7

rows = []
bad = 0
for line in (out / 'uart.log').read_bytes().splitlines()[1:-1]:
    if not line.startswith(b'@V6 '):
        continue
    try:
        _, length, crc, payload = line.rstrip(b'\r').split(b' ', 3)
        assert len(payload) == int(length) and zlib.crc32(payload) == int(crc, 16)
        rows.append(json.loads(payload))
    except (ValueError, AssertionError):
        bad += 1
assert len(rows) >= 5 and rows[-1]['detection_seq'] > rows[0]['detection_seq']
assert all(len(r['tracks']) <= 1 for r in rows)
for r in rows:
    d=r['detection_chain']
    assert d['displayed']==len(r['tracks'])
    assert 0<=d['tracked']<=d['displayed'] and 0<=d['mapped']<=min(16,d['candidates'])<=d['decoded']
    assert len(d['boxes'])==min(16,d['decoded'])
    assert (d['selected_source']>=0)==bool(d['displayed'])
reasons={k:0 for k in ('invalid_snapshot','no_decoded','candidate_rejected','mapping_empty','selection_rejected','displayed')}
for r in rows:
    d=r['detection_chain']
    reason=('invalid_snapshot' if not d['snapshot_valid'] else 'no_decoded' if not d['decoded'] else
            'candidate_rejected' if not d['candidates'] else 'mapping_empty' if not d['mapped'] else
            'selection_rejected' if d['selected_source']<0 else 'displayed')
    reasons[reason]+=1
(out / 'telemetry.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
(out / 'previews.json').write_text(json.dumps(previews, indent=2), encoding='utf-8')
summary = dict(chain_counts=reasons,packets=len(rows), bad_complete_packets=bad, images=len(previews), errors=errors,
               detections_with_box=sum(bool(r['tracks']) for r in rows),
               unique_ids=sorted({t['id'] for r in rows for t in r['tracks']}),
               capture_span_s=(rows[-1]['capture_us']-rows[0]['capture_us'])/1e6,
               limitations='Diagnostic stages share UART frame/capture timestamp. Decoded boxes are AFTER threshold and NMS. Preview/status/UART may differ; an empty detection is not automatically a miss. No labeled accuracy measurement.',
               reset=False, downloaded=False, flash_written=False)
(out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary), flush=True)
