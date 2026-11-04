"""Reuse the tested hardware UDP receiver; serve only complete, distinct MJPEG frames."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import time
import urllib.request
import webbrowser
from video_stream import VideoStream


class BoardState:
    def __init__(self):
        self.lock = threading.Lock()
        self.current = {}
        self.received = 0
        self.running = True
        self.thread = threading.Thread(target=self.poll, daemon=True)
        self.thread.start()

    def snapshot(self):
        with self.lock:
            return self.current if time.monotonic() - self.received < 2 else {}

    def poll(self):
        while self.running:
            try:
                with urllib.request.urlopen('http://192.168.0.2/api/status', timeout=2) as response:
                    raw = response.read(32769)
                if len(raw) > 32768:
                    raise ValueError('status too large')
                s = json.loads(raw)
                if s.get('server') != 'riscv-lwip':
                    raise ValueError('not board status')
                power = s.get('power', {})
                if not isinstance(power, dict):
                    raise ValueError('invalid power status')
                tracks = []
                for t in s['tracks'][:16]:
                    x0, y0, x1, y1 = t.get('display_box', t['box'])
                    if not (t['state'] in (0, 1, 2) and 0 <= x0 <= x1 < 1920 and 0 <= y0 <= y1 < 1080):
                        raise ValueError('invalid box')
                    if t['state'] == 0 and t.get('person_confirmed') == 0:
                        continue
                    tracks.append(dict(id=t['id'], state=('NORMAL', 'FALLING', 'FALLEN')[t['state']],
                                       box=(x0/1920, y0/1080, x1/1920, y1/1080)))
                with self.lock:
                    self.current = dict(connected=True, capture_ms=s['capture_us']/1000, tracks=tracks,
                                        low_power=power.get('mode') == 'watch' and not s['tracks'])
                    self.received = time.monotonic()
            except (OSError, ValueError, KeyError, TypeError):
                pass
            time.sleep(.5)


class VideoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('Host') != f'{self.server.server_address[0]}:{self.server.server_port}':
            self.send_error(403)
            return
        path = self.path.split('?', 1)[0]
        if path == '/api/video':
            info = self.server.stream.info()
            info['url'] = f'http://{self.server.server_address[0]}:{self.server.server_port}/api/stream.mjpg' if info['connected'] else ''
            body = json.dumps(info).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Access-Control-Allow-Origin', 'http://192.168.0.2')
            self.end_headers()
            self.wfile.write(body)
        elif path == '/api/frame.jpg':
            with self.server.stream.condition:
                latest = self.server.stream.latest
            if not latest or time.monotonic() - latest[3] > 1.5:
                self.send_error(503, 'Waiting for a fresh camera frame')
                return
            frame, stamp, jpeg, _ = latest
            self.send_response(200)
            self.send_header('Content-Type', 'image/jpeg')
            self.send_header('Content-Length', str(len(jpeg)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Access-Control-Allow-Origin', 'http://192.168.0.2')
            self.send_header('X-Frame-Id', str(frame))
            self.send_header('X-Capture-Us', str(stamp))
            self.send_header('Access-Control-Expose-Headers', 'X-Frame-Id, X-Capture-Us')
            self.end_headers()
            self.wfile.write(jpeg)
        elif path == '/api/stream.mjpg':
            stream = self.server.stream
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.connection.settimeout(2)
            previous = -1
            try:
                while stream.running:
                    with stream.condition:
                        stream.condition.wait_for(lambda: stream.sequence != previous or not stream.running, timeout=.5)
                        if not stream.running or not stream.latest or time.monotonic()-stream.latest[3] > 1.5:
                            break
                        if stream.sequence == previous:
                            continue
                        previous = stream.sequence
                        frame, stamp, jpeg, _ = stream.latest
                    header = f'--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {len(jpeg)}\r\nX-Frame-Id: {frame}\r\nX-Capture-Us: {stamp}\r\n\r\n'
                    self.wfile.write(header.encode() + jpeg + b'\r\n')
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, TimeoutError):
                pass
            self.close_connection = True
        else:
            self.send_error(404)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description='FPGA UDP8090 -> MJPEG8093 for board website')
    parser.add_argument('--listen', default='192.168.0.3')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    board = BoardState()
    try:
        stream = VideoStream((args.listen, 8090), board.snapshot)
        server = ThreadingHTTPServer((args.listen, 8093), VideoHandler)
    except OSError as exc:
        raise SystemExit(f'视频接收无法启动：{exc}。检查有线IPv4=192.168.0.3，并关闭旧UDP8090接收程序。') from exc
    server.stream = stream
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    print('网口视频接收：UDP8090；网页视频：HTTP8093\n打开 http://192.168.0.2/ ，按 Ctrl+C 停止。', flush=True)
    if not args.no_browser:
        webbrowser.open('http://192.168.0.2/')
    try:
        while True:
            info = stream.info()
            print(f"{'已接收' if info['connected'] else '等待板卡'}  实际{info['fps']:.1f}帧/秒  完整帧{info['frames']}  坏包{info['bad_packets']}  不完整帧{info['incomplete_frames']}", flush=True)
            time.sleep(2)
    except KeyboardInterrupt:
        pass
    finally:
        board.running = False
        stream.close()
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)


if __name__ == '__main__':
    main()
