import io
import json
import socket
import struct
import threading
import time
import unittest
from unittest.mock import patch
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from PIL import Image
from ethernet_video import BoardState, VideoHandler
from video_stream import FrameAssembler, VideoStream


def packet(frame, row, stamp=1000000):
    return struct.pack('!4sII4H', b'V6V1', frame, stamp, 320, 180, row, 2) + bytes((row, 20, 30))*320


class VideoTest(unittest.TestCase):
    def test_infrared_confirmation_preserves_alerts_and_rgb_only_boxes(self):
        state = BoardState.__new__(BoardState)
        state.lock = threading.Lock()
        state.current = {}
        state.received = 0
        raw = dict(server='riscv-lwip', capture_us=1000000, power={'mode': 'watch'},
                   tracks=[dict(id=i, state=s, person_confirmed=c, box=[100,80,900,1000])
                           for i,s,c in ((1,0,0),(2,0,1),(3,1,0),(4,2,0))])
        state.running = True
        with patch('ethernet_video.urllib.request.urlopen', return_value=io.BytesIO(json.dumps(raw).encode())), \
                patch('ethernet_video.time.sleep', side_effect=lambda _: setattr(state, 'running', False)):
            state.poll()
        self.assertEqual([t['id'] for t in state.snapshot()['tracks']], [2,3,4])
        self.assertFalse(state.snapshot()['low_power'])

    def test_board_http_boxes_expire_and_invalid_state_is_rejected(self):
        state = BoardState.__new__(BoardState)
        state.lock = threading.Lock()
        state.current = {}
        state.received = 0
        state.running = True
        raw = dict(server='riscv-lwip', capture_us=1000000, power={'mode': 'watch'},
                   tracks=[dict(id=3, state=2, box=[100, 80, 900, 1000], display_box=[120, 100, 920, 1020])])
        with patch('ethernet_video.urllib.request.urlopen', return_value=io.BytesIO(json.dumps(raw).encode())), \
                patch('ethernet_video.time.sleep', side_effect=lambda _: setattr(state, 'running', False)):
            state.poll()
        current = state.snapshot()
        self.assertEqual(current['tracks'][0]['state'], 'FALLEN')
        self.assertFalse(current['low_power'], 'person presence prevents a low-power label')
        self.assertEqual(current['tracks'][0]['box'], (120/1920, 100/1080, 920/1920, 1020/1080))
        state.received = time.monotonic()-3
        self.assertEqual(state.snapshot(), {})
        raw['tracks'] = []
        state.running = True
        with patch('ethernet_video.urllib.request.urlopen', return_value=io.BytesIO(json.dumps(raw).encode())), \
                patch('ethernet_video.time.sleep', side_effect=lambda _: setattr(state, 'running', False)):
            state.poll()
        self.assertTrue(state.snapshot()['low_power'])
        raw['tracks'] = [dict(id=3, state=2, box=[100, 80, 900, 1000])]
        state.current = {}
        state.running = True
        raw['tracks'][0]['state'] = 3
        with patch('ethernet_video.urllib.request.urlopen', return_value=io.BytesIO(json.dumps(raw).encode())), \
                patch('ethernet_video.time.sleep', side_effect=lambda _: setattr(state, 'running', False)):
            state.poll()
        self.assertEqual(state.snapshot(), {})

    def test_reorder_duplicate_and_missing_rows(self):
        assembler = FrameAssembler()
        for row in reversed(range(180)):
            result = assembler.feed(packet(7, row), 10)
        self.assertEqual(result, (7, 1000000, b''.join(bytes((row, 20, 30))*320 for row in range(180))))
        self.assertIsNone(assembler.feed(packet(7, 0), 10.1))
        for row in range(179):
            self.assertIsNone(assembler.feed(packet(8, row), 10.2))
        self.assertIsNone(assembler.feed(packet(9, 0), 10.8))
        self.assertEqual(assembler.incomplete, 1)

    def test_invalid_conflicting_packets_and_restart(self):
        a = FrameAssembler()
        for invalid in (b'', packet(1, 180), b'EVIL'+packet(1, 0)[4:], packet(1, 0)[:-1]):
            with self.assertRaises(ValueError):
                a.feed(invalid, 10)
        a.feed(packet(100, 0), 10)
        with self.assertRaises(ValueError):
            a.feed(packet(100, 0, 1), 10)
        for frame in range(101, 110):
            a.feed(packet(frame, 0), 10)
        self.assertLessEqual(len(a.pending), 2)
        for row in range(180):
            result = a.feed(packet(1, row), 13)
        self.assertEqual(result[0], 1)

    def test_real_udp_complete_frames_and_http_mjpeg(self):
        # Bind loopback; socket sender below must use the same source filter as the board.
        board_state = {}
        stream = VideoStream(('127.0.0.1', 0), lambda: board_state)
        server = ThreadingHTTPServer(('127.0.0.1', 0), VideoHandler)
        server.stream = stream
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        original_socket = stream.socket
        # The production receiver filters 192.168.0.2. A wrapper changes only the test peer address.
        class LocalPeer:
            def recvfrom(self, size):
                data, _ = original_socket.recvfrom(size)
                return data, ('192.168.0.2', 8090)
            def close(self):
                original_socket.close()
        stream.socket = LocalPeer()
        address = original_socket.getsockname()
        connection = HTTPConnection(*server.server_address, timeout=3)
        try:
            connection.request('GET', '/api/frame.jpg')
            response = connection.getresponse()
            self.assertEqual(response.status, 503)
            response.read()
            # Let the initial blocking receive return and pick up the local peer wrapper.
            time.sleep(.25)
            for frame in range(1, 4):
                for row in range(180):
                    sender.sendto(packet(frame, row, frame*100000), address)
                    if row % 30 == 0:
                        time.sleep(.001)
                end = time.monotonic()+2
                with stream.condition:
                    stream.condition.wait_for(lambda: stream.sequence == frame, timeout=2)
                self.assertEqual(stream.sequence, frame)
                time.sleep(.09)
            connection.request('GET', '/api/video', headers={'Origin': 'http://192.168.0.2'})
            response = connection.getresponse()
            self.assertEqual(response.getheader('Access-Control-Allow-Origin'), 'http://192.168.0.2')
            info = json.loads(response.read())
            self.assertTrue(info['connected'])
            self.assertEqual(info['frames'], 3)
            self.assertTrue(5 < info['fps'] < 15)
            connection.request('GET', '/api/frame.jpg?t=1', headers={'Origin': 'http://192.168.0.2'})
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(response.getheader('X-Frame-Id'), '3')
            self.assertEqual(response.getheader('X-Capture-Us'), '300000')
            self.assertEqual(response.getheader('Access-Control-Allow-Origin'), 'http://192.168.0.2')
            self.assertIn('X-Frame-Id', response.getheader('Access-Control-Expose-Headers'))
            self.assertEqual(response.getheader('Cache-Control'), 'no-store')
            self.assertEqual(response.read(), stream.latest[2])
            connection.request('GET', '/api/stream.mjpg')
            response = connection.getresponse()
            self.assertIn('multipart/x-mixed-replace', response.getheader('Content-Type'))
            headers = bytearray()
            while not headers.endswith(b'\r\n\r\n'):
                headers.extend(response.read(1))
            self.assertIn(b'X-Frame-Id: 3', headers)
            size = int(next(line.split(b':')[1] for line in headers.split(b'\r\n') if line.startswith(b'Content-Length:')))
            image = Image.open(io.BytesIO(response.read(size)))
            self.assertEqual(image.size, (320, 180))
            board_state.update(connected=True, low_power=True, tracks=[])
            for frame in (4, 5):
                if frame == 5:
                    time.sleep(.5)
                for row in range(180):
                    sender.sendto(packet(frame, row, frame*500000), address)
                    if row % 30 == 0:
                        time.sleep(.001)
                with stream.condition:
                    self.assertTrue(stream.condition.wait_for(lambda: stream.sequence == frame, timeout=2))
            self.assertTrue(stream.info()['low_power'])
            self.assertEqual(stream.info()['target_fps'], 2)
            self.assertTrue(1.5 < stream.info()['fps'] < 2.5)
            board_state.clear()  # Expired board telemetry must not retain a low-power label.
            for row in range(180):
                sender.sendto(packet(6, row, 2600000), address)
                if row % 30 == 0:
                    time.sleep(.001)
            with stream.condition:
                self.assertTrue(stream.condition.wait_for(lambda: stream.sequence == 6, timeout=2))
            self.assertFalse(stream.info()['low_power'])
            self.assertEqual(stream.info()['target_fps'], 10)
            with stream.condition:
                stream.latest = (*stream.latest[:3], time.monotonic()-2)
            self.assertFalse(stream.info()['connected'])
            self.assertEqual(stream.info()['fps'], 0)
            snapshot = HTTPConnection(*server.server_address, timeout=3)
            try:
                snapshot.request('GET', '/api/frame.jpg')
                response = snapshot.getresponse()
                self.assertEqual(response.status, 503)
                response.read()
            finally:
                snapshot.close()
        finally:
            connection.close()
            sender.close()
            stream.close()
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == '__main__':
    unittest.main()
