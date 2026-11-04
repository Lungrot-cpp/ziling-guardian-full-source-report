"""Real local TCP/storage/HTTP tests. All generated event data stays in temporary folders."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest
import urllib.request
from http.server import ThreadingHTTPServer
from event_receiver import HEADER, PIXELS, Receiver, Store, Viewer, bitmap, read_exact


def fixture(event_id=1, state=2):
    fields = (event_id, 3, state, 12345, 77, 850, 700, 780, 1, 100, 80, 900, 1000, 0, 96, 54)
    return HEADER.pack(b'EVT2', *fields) + bytes(i % 251 for i in range(PIXELS))


class ReceiverTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.store = Store(self.folder.name)
        self.server = Receiver(('127.0.0.1', 0), self.store, '127.0.0.1')
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.store.db.close()
        self.folder.cleanup()

    def connect(self, token=bytes(16)):
        stream = socket.create_connection(self.server.server_address, timeout=2)
        stream.settimeout(2)
        stream.sendall(b'EVH2' + token)
        hello = read_exact(stream, 20)
        self.assertEqual(hello[:4], b'EVS2')
        self.assertNotEqual(hello[4:], bytes(16))
        return stream, hello[4:]

    def test_fragmentation_image_fields_and_files(self):
        stream, token = self.connect()
        with stream:
            payload = fixture()
            for at in range(0, len(payload), 137):
                stream.sendall(payload[at:at + 137])
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\1')
            stream.sendall(b'EVP2\0\0\0\0')
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\0')
        event = self.store.summary()['events'][0]
        self.assertEqual((event['id'], event['frame'], event['uptime_ms'], event['detector']), (1, 77, 12345, 850))
        image = Path(self.folder.name, 'thumbnails', token.hex() + '_1.bmp').read_bytes()
        self.assertEqual(image, bitmap(payload[HEADER.size:]))
        self.assertIn('规则报警', Path(self.folder.name, 'events.csv').read_text(encoding='utf-8-sig'))

    def test_rgb_same_frame_and_legacy_grayscale(self):
        stream, token = self.connect()
        rgb = bytes((255, 17, 3)) * PIXELS
        header = HEADER.pack(b'EVT3', *HEADER.unpack(fixture()[:HEADER.size])[1:])
        with stream:
            for at in range(0, len(header + rgb), 137):
                stream.sendall((header + rgb)[at:at + 137])
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\1')
            stream.sendall(fixture(event_id=2))
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\2')
        image = Path(self.folder.name, 'thumbnails', token.hex() + '_1.bmp').read_bytes()
        self.assertEqual(int.from_bytes(image[28:30], 'little'), 24)
        self.assertEqual(image[54:], bytes((3, 17, 255)) * PIXELS)
        self.assertEqual([e['color'] for e in self.store.summary()['events']], [False, True])
        self.store.export()
        self.assertEqual(image, bitmap(rgb))

    def test_lost_application_ack_reconnect_deduplicates(self):
        stream, token = self.connect()
        stream.sendall(fixture())
        stream.shutdown(socket.SHUT_WR)
        stream.close()
        stream, same = self.connect(token)
        with stream:
            stream.sendall(fixture())
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\1')
        self.assertEqual(token, same)
        self.assertEqual(self.store.summary()['count'], 1)
        self.assertEqual(self.store.summary()['duplicates'], 1)

    def test_reboot_new_session_same_id_and_persistence(self):
        tokens = []
        for _ in range(2):
            stream, token = self.connect()
            tokens.append(token)
            with stream:
                stream.sendall(fixture())
                read_exact(stream, 8)
        self.assertNotEqual(*tokens)
        second = Store(self.folder.name)
        try:
            self.assertEqual(second.summary()['count'], 2)
            self.assertEqual(len(list(Path(self.folder.name, 'thumbnails').glob('*.bmp'))), 2)
        finally:
            second.db.close()

    def test_invalid_and_conflicting_duplicate_rejected(self):
        stream, token = self.connect()
        with stream:
            stream.sendall(fixture(state=3))
            self.assertEqual(stream.recv(8), b'')
        self.assertEqual(self.store.summary()['count'], 0)
        stream, token = self.connect(token)
        with stream:
            stream.sendall(fixture())
            read_exact(stream, 8)
            stream.sendall(fixture(state=1))
            self.assertEqual(stream.recv(8), b'')
        self.assertEqual(self.store.summary()['count'], 1)

    def test_commit_failure_has_no_ack(self):
        def failed(*_):
            raise OSError('simulated disk failure')
        self.store.save = failed
        stream, _ = self.connect()
        with stream:
            stream.sendall(fixture())
            self.assertEqual(stream.recv(8), b'')

    def test_export_failure_keeps_committed_event_and_ack(self):
        export = self.store.export_locked
        def locked():
            raise PermissionError('CSV locked by spreadsheet')
        self.store.export_locked = locked
        stream, _ = self.connect()
        with stream:
            stream.sendall(fixture())
            self.assertEqual(read_exact(stream, 8), b'EVA2\0\0\0\1')
        self.assertEqual(self.store.summary()['count'], 1)
        self.assertIsNotNone(self.store.summary()['export_error'])
        self.store.export_locked = export
        self.store.export()
        self.assertIn('规则报警', Path(self.folder.name, 'events.csv').read_text(encoding='utf-8-sig'))

    def test_receiver_restart_preserves_session_and_deduplication(self):
        stream, token = self.connect()
        with stream:
            stream.sendall(fixture())
            read_exact(stream, 8)
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.server = Receiver(('127.0.0.1', 0), self.store, '127.0.0.1')
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        stream, same = self.connect(token)
        with stream:
            stream.sendall(fixture())
            read_exact(stream, 8)
        self.assertEqual(same, token)
        self.assertEqual(self.store.summary()['count'], 1)

    def test_http_exports_images_and_empty_real_state(self):
        web = ThreadingHTTPServer(('127.0.0.1', 0), Viewer)
        web.store = self.store
        thread = threading.Thread(target=web.serve_forever, daemon=True)
        thread.start()
        url = f'http://127.0.0.1:{web.server_port}'
        try:
            with urllib.request.urlopen(url + '/api/events') as response:
                self.assertEqual(json.load(response)['events'], [])
                self.assertEqual(response.headers['Access-Control-Allow-Origin'], 'http://192.168.0.2')
            with urllib.request.urlopen(url) as response:
                self.assertIn('真实检测'.encode(), response.read())
            stream, token = self.connect()
            with stream:
                stream.sendall(fixture())
                read_exact(stream, 8)
            with urllib.request.urlopen(url + f'/image/{token.hex()}/1.bmp') as response:
                self.assertEqual(response.read(), bitmap(fixture()[HEADER.size:]))
            with urllib.request.urlopen(url + '/events.csv') as response:
                self.assertIn(b'received_utc', response.read())
            request = urllib.request.Request(url, headers={'Host': 'untrusted.example'})
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(request)
            self.assertEqual(error.exception.code, 403)
        finally:
            web.shutdown()
            web.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
