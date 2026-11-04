"""Receive real board events over TCP; acknowledge only after durable SQLite commit."""
import argparse
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import socketserver
import sqlite3
import struct
import threading
import time
import uuid
import webbrowser

FIELDS = ('id', 'track', 'state', 'uptime_ms', 'frame', 'detector', 'rule',
          'score', 'weighted', 'x0', 'y0', 'x1', 'y1', 'quality', 'width', 'height')
HEADER = struct.Struct('!4s16I')
PIXELS = 96 * 54
STATES = ('正常', '疑似异常', '规则报警')


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        part = stream.recv(size - len(data))
        if not part:
            raise EOFError('connection closed')
        data.extend(part)
    return bytes(data)


def decode(header, picture):
    magic, *values = HEADER.unpack(header)
    e = dict(zip(FIELDS, values))
    if (magic not in (b'EVT2', b'EVT3') or not e['id'] or not e['track'] or e['state'] > 2
            or max(e['detector'], e['rule'], e['score']) > 1000 or e['weighted'] > 1
            or not (e['x0'] <= e['x1'] < 1920 and e['y0'] <= e['y1'] < 1080)
            or (e['width'], e['height']) != (96, 54) or len(picture) != PIXELS * (3 if magic == b'EVT3' else 1)):
        raise ValueError('invalid event fields')
    return e


def bitmap(pixels):
    if len(pixels) == PIXELS * 3:
        bgr = bytearray(pixels)
        bgr[0::3], bgr[2::3] = pixels[2::3], pixels[0::3]
        return (struct.pack('<2sIHHI', b'BM', 54 + len(bgr), 0, 0, 54)
                + struct.pack('<IiiHHIIiiII', 40, 96, -54, 1, 24, 0, len(bgr), 0, 0, 0, 0) + bgr)
    palette = b''.join(bytes((v, v, v, 0)) for v in range(256))
    offset = 14 + 40 + len(palette)
    return (struct.pack('<2sIHHI', b'BM', offset + PIXELS, 0, 0, offset)
            + struct.pack('<IiiHHIIiiII', 40, 96, -54, 1, 8, 0, PIXELS, 0, 0, 256, 0)
            + palette + pixels)


class Store:
    def __init__(self, folder):
        self.folder = Path(folder)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.db = sqlite3.connect(self.folder / 'events.sqlite3', check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('CREATE TABLE IF NOT EXISTS events (session TEXT, id INTEGER, received TEXT, '
                        'metadata TEXT, picture BLOB, PRIMARY KEY(session,id))')
        self.db.commit()
        self.connections = 0
        self.last_contact = None
        self.duplicates = 0
        self.export_error = None
        self.export()

    def save(self, session, header, picture):
        e = decode(header, picture)
        metadata = json.dumps(e, separators=(',', ':'))
        with self.lock:
            previous = self.db.execute('SELECT metadata,picture FROM events WHERE session=? AND id=?',
                                       (session, e['id'])).fetchone()
            if previous:
                if previous != (metadata, picture):
                    raise ValueError('conflicting duplicate event')
                self.duplicates += 1
            else:
                received = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
                with self.db:
                    self.db.execute('INSERT INTO events VALUES (?,?,?,?,?)',
                                    (session, e['id'], received, metadata, picture))
                print(f"{received}  event={e['id']} track={e['track']} {STATES[e['state']]} "
                      f"frame={e['frame']} box=({e['x0']},{e['y0']},{e['x1']},{e['y1']}) "
                      f"detector={e['detector'] / 10:.1f}% score={e['score'] / 1000:.3f}", flush=True)
            try:
                self.export_locked()
                self.export_error = None
            except OSError as exc:
                self.export_error = str(exc)
                print(f'CSV/BMP export failed; committed data remains in events.sqlite3: {exc}', flush=True)
            return e['id']

    def export(self):
        with self.lock:
            self.export_locked()

    def export_locked(self):
        # SQLite is authoritative; regenerating exports also repairs interrupted file writes.
        rows = self.db.execute('SELECT session,id,received,metadata,picture FROM events ORDER BY received,rowid').fetchall()
        images = self.folder / 'thumbnails'
        images.mkdir(exist_ok=True)
        csv_path = self.folder / 'events.csv'
        with csv_path.with_suffix('.tmp').open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(('session', 'received_utc', 'type', *FIELDS, 'thumbnail'))
            for session, event_id, received, metadata, picture in rows:
                e = json.loads(metadata)
                name = f'{session}_{event_id}.bmp'
                path = images / name
                if not path.exists():
                    temporary = path.with_suffix('.tmp')
                    temporary.write_bytes(bitmap(picture))
                    temporary.replace(path)
                writer.writerow((session, received, STATES[e['state']], *(e[k] for k in FIELDS), 'thumbnails/' + name))
        csv_path.with_suffix('.tmp').replace(csv_path)

    def summary(self):
        with self.lock:
            rows = self.db.execute('SELECT session,received,metadata,length(picture) FROM events ORDER BY received DESC,rowid DESC LIMIT 100').fetchall()
            return dict(connected=self.connections > 0, connections=self.connections, last_contact=self.last_contact,
                        duplicates=self.duplicates, export_error=self.export_error,
                        count=self.db.execute('SELECT count(*) FROM events').fetchone()[0],
                        events=[dict(json.loads(meta), session=session, received_utc=received, color=size == PIXELS * 3)
                                for session, received, meta, size in rows])


class Receiver(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, store, board='192.168.0.2'):
        self.store, self.board = store, board
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(address, EventHandler)

    def verify_request(self, request, client_address):
        return client_address[0] == self.board


class EventHandler(socketserver.BaseRequestHandler):
    def handle(self):
        if not self.server.slots.acquire(False):
            return
        store = self.server.store
        active = False
        try:
            self.request.settimeout(20)
            hello = read_exact(self.request, 20)
            if hello[:4] != b'EVH2':
                raise ValueError('invalid handshake')
            token = hello[4:]
            if token == bytes(16):
                token = uuid.uuid4().bytes
            self.request.sendall(b'EVS2' + token)
            session = token.hex()
            with store.lock:
                store.connections += 1
                active = True
            while True:
                magic = read_exact(self.request, 4)
                if magic == b'EVP2':
                    if read_exact(self.request, 4) != bytes(4):
                        raise ValueError('invalid heartbeat')
                    event_id = 0
                elif magic in (b'EVT2', b'EVT3'):
                    header = magic + read_exact(self.request, HEADER.size - 4)
                    # Reject invalid dimensions/metadata before receiving the bounded image.
                    size = PIXELS * (3 if magic == b'EVT3' else 1)
                    decode(header, bytes(size))
                    event_id = store.save(session, header, read_exact(self.request, size))
                else:
                    raise ValueError('unknown message')
                with store.lock:
                    store.last_contact = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
                self.request.sendall(b'EVA2' + struct.pack('!I', event_id))
        except (OSError, EOFError, ValueError, sqlite3.Error) as exc:
            if not isinstance(exc, EOFError):
                print(f'Report connection ended: {exc}', flush=True)
        finally:
            if active:
                with store.lock:
                    store.connections -= 1
            self.server.slots.release()


class Viewer(BaseHTTPRequestHandler):
    def do_GET(self):
        hosts = ('127.0.0.1', 'localhost', self.server.server_address[0])
        if self.headers.get('Host') not in tuple(f'{host}:{self.server.server_port}' for host in hosts):
            self.send_error(403)
            return
        path = self.path.split('?', 1)[0]
        if path == '/':
            body = Path(__file__).with_name('event_receiver.html').read_bytes()
            mime = 'text/html; charset=utf-8'
        elif path == '/api/events':
            body = json.dumps(self.server.store.summary(), ensure_ascii=False).encode()
            mime = 'application/json; charset=utf-8'
        elif path == '/events.csv':
            with self.server.store.lock:
                body = (self.server.store.folder / 'events.csv').read_bytes()
            mime = 'text/csv; charset=utf-8'
        elif match := re.fullmatch(r'/image/([0-9a-f]{32})/([1-9][0-9]{0,9})\.bmp', path):
            with self.server.store.lock:
                row = self.server.store.db.execute('SELECT picture FROM events WHERE session=? AND id=?', match.groups()).fetchone()
            if not row:
                self.send_error(404)
                return
            body, mime = bitmap(row[0]), 'image/bmp'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Access-Control-Allow-Origin', 'http://192.168.0.2')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description='Ti60 Ethernet event receiver; real data only')
    parser.add_argument('--listen', default='192.168.0.3')
    parser.add_argument('--board', default='192.168.0.2')
    parser.add_argument('--port', type=int, default=8091)
    parser.add_argument('--web-port', type=int, default=8092)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / '记录' / '以太网事件')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    store = Store(args.output)
    try:
        receiver = Receiver((args.listen, args.port), store, args.board)
        viewer = ThreadingHTTPServer(('127.0.0.1', args.web_port), Viewer)
        board_viewer = ThreadingHTTPServer((args.listen, args.web_port), Viewer) if args.listen != '127.0.0.1' else None
    except OSError as exc:
        raise SystemExit(f'Cannot start receiver: {exc}. Check wired IPv4=192.168.0.3 and port availability.') from exc
    viewer.store = store
    if board_viewer:
        board_viewer.store = store
        threading.Thread(target=board_viewer.serve_forever, daemon=True).start()
    threading.Thread(target=receiver.serve_forever, daemon=True).start()
    threading.Thread(target=viewer.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{args.web_port}/'
    print(f'监听 {args.listen}:{args.port}，只接收板卡 {args.board}\n展示 {url}\n存档 {store.folder.resolve()}\nCtrl+C 停止；重启继续保存。', flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print('接收已停止，已确认事件保存在 SQLite，CSV/BMP 可在下次启动时恢复。', flush=True)
    finally:
        receiver.shutdown()
        viewer.shutdown()
        receiver.server_close()
        viewer.server_close()
        if board_viewer:
            board_viewer.shutdown()
            board_viewer.server_close()


if __name__ == '__main__':
    main()
