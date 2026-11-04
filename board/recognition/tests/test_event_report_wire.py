"""Production RV32 reporter -> real Ethernet/TCP packets -> PC SQLite receiver store.

NIC MMIO and the peer are emulated. No physical board/link is claimed.
"""
from pathlib import Path
import json
import runpy
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'tools'))
from event_receiver import HEADER, PIXELS, Store, decode

sys.argv.append('--event-report-check')
environment = runpy.run_path(str(ROOT / 'tests/test_care_http.py'))
g = environment['run'].__globals__
u, run, inject = g['u'], g['run'], g['inject']
out, checksum = g['out'], g['checksum']
pcip, boardip, pcmac, boardmac = (g[k] for k in ('pcip', 'boardip', 'pcmac', 'boardmac'))


def packets():
    result = []
    for packet in out[:]:
        if packet[12:14] != b'\x08\x00' or packet[23] != 6:
            continue
        ip = packet[14:34]
        tcp = packet[34:14 + int.from_bytes(ip[2:4], 'big')]
        if int.from_bytes(tcp[2:4], 'big') != 8091:
            continue
        assert checksum(ip) == 0
        assert checksum(boardip + pcip + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp) == 0
        source, _, sequence, _, offset, flags, _, _, _ = struct.unpack('!HHIIBBHHH', tcp[:20])
        result.append((source, sequence, flags, tcp[(offset >> 4) * 4:]))
    out.clear()
    return result


class Peer:
    def __init__(self):
        self.port = None
        self.seq = 100000
        self.ack = 0
        self.buffer = bytearray()

    def send(self, payload=b'', flags=0x18):
        tcp = struct.pack('!HHIIBBHHH', 8091, self.port, self.seq, self.ack, 5 << 4, flags, 65535, 0, 0) + payload
        check = checksum(pcip + boardip + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp)
        tcp = tcp[:16] + struct.pack('!H', check) + tcp[18:]
        ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), 1, 0x4000, 64, 6, 0, pcip, boardip)
        ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
        inject(boardmac + pcmac + b'\x08\x00' + ip + tcp)
        self.seq += len(payload) + bool(flags & 2)

    def drain(self):
        for port, sequence, flags, payload in packets():
            if port != self.port:
                continue
            if payload:
                if sequence == self.ack:
                    self.buffer.extend(payload)
                    self.ack += len(payload)
                self.send(flags=0x10)

    def read(self, size):
        for _ in range(300):
            self.drain()
            if len(self.buffer) >= size:
                data = bytes(self.buffer[:size])
                del self.buffer[:size]
                return data
            run()
        raise AssertionError(('missing reporter bytes', size, len(self.buffer)))

    def connect(self, token):
        for _ in range(40):
            g['ticks'] += 100000000
            run()
            syn = next((p for p in packets() if p[2] & 2), None)
            if syn:
                self.port, sequence, _, _ = syn
                self.ack = sequence + 1
                self.send(flags=0x12)
                break
        else:
            raise AssertionError('reporter did not reconnect')
        hello = self.read(20)
        assert hello[:4] == b'EVH2'
        assert hello[4:] == (bytes(16) if token is None else token)
        token = bytes.fromhex('00112233445566778899aabbccddeeff') if token is None else token
        self.send(b'EVS2' + token[:3])
        self.send(token[3:])
        return token


# Fill the bounded queue through the actual C++ capture code; preserve oldest entries.
u.mem_write(0xf8100348, struct.pack('<I', 1))
run()
_, data = g['request']([b'GET /api/report HTTP/1.1\r\n\r\n'])
stats = json.loads(data)
assert (stats['queued'], stats['dropped'], stats['confirmed']) == (16, 2, 0)
expected = bytearray()
rgb = bytes(i % 251 for i in range(96 * 96 * 3))
for i in range(PIXELS):
    at = ((((2 * (i // 96) + 1) * 96) // (2 * 54)) * 96 + i % 96) * 3
    expected.extend(rgb[at:at + 3])

with tempfile.TemporaryDirectory() as folder:
    store = Store(folder)
    peer = Peer()
    token = peer.connect(None)
    first = peer.read(HEADER.size + PIXELS * 3)
    assert first[:4] == b'EVT3'
    assert decode(first[:HEADER.size], first[HEADER.size:])['id'] == 1
    assert first[HEADER.size:] == expected
    store.save(token.hex(), first[:HEADER.size], first[HEADER.size:])
    # The PC committed event 1, but its application ACK was lost.
    peer.send(flags=0x14)
    peer = Peer()
    assert peer.connect(token) == token
    again = peer.read(len(first))
    assert again == first
    store.save(token.hex(), again[:HEADER.size], again[HEADER.size:])
    # A wrong event ACK must not release the queue head.
    peer.send(b'EVA2' + struct.pack('!I', 99))
    peer = Peer()
    peer.connect(token)
    assert peer.read(len(first)) == first
    peer.send(b'EVA')
    peer.send(b'2' + struct.pack('!I', 1))
    for event_id in range(2, 17):
        frame = peer.read(len(first))
        e = decode(frame[:HEADER.size], frame[HEADER.size:])
        assert (e['id'], e['state'], e['uptime_ms'], e['frame']) == (event_id, event_id % 3, 12345 + event_id, 77)
        assert frame[HEADER.size:] == expected
        store.save(token.hex(), frame[:HEADER.size], frame[HEADER.size:])
        peer.send(b'EVA2' + struct.pack('!I', event_id))
    peer.drain()
    g['ticks'] += 500100000
    run()
    assert peer.read(8) == b'EVP2' + bytes(4)
    peer.send(b'EVA2' + bytes(4))
    _, data = g['request']([b'GET /api/report HTTP/1.1\r\n\r\n'])
    stats = json.loads(data)
    assert (stats['queued'], stats['confirmed'], stats['dropped'], stats['last_confirmed_id']) == (0, 16, 2, 16), stats
    assert stats['connected'] is True
    assert stats['session'] == token.hex()
    assert store.summary()['count'] == 16 and store.summary()['duplicates'] == 1
    store.db.close()
report = dict(passed=True, physical_board_tested=False, events=16, overflow_reported=2,
              actual_rv32_production_objects=True, tcp_checksums=True, exact_event_frame_thumbnail=True,
              reconnect=True, lost_ack_resend=True, wrong_ack_retains_event=True,
              fragmented_session_and_ack=True, heartbeat=True, pc_sqlite_deduplication=True,
              board_status=stats)
(ROOT.parent / 'validation/ethernet-wire-tests.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('ETHERNET_REPORT_WIRE_PASS', json.dumps(report))
