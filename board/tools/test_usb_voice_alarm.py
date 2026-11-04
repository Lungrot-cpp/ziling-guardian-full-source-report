"""Offline checks: no physical serial port is opened."""
import contextlib
import io
import json
import unittest
import zlib
from types import SimpleNamespace
from unittest.mock import patch

import usb_voice_alarm as app


def packet(seq, states, **extra):
    body = json.dumps(dict(v=1, seq=seq, tracks=[dict(state=s) for s in states], **extra)).encode()
    return b'@V6 %d %08x ' % (len(body), zlib.crc32(body)) + body + b'\n'


class VoiceTests(unittest.TestCase):
    def test_exact_alarm_message(self):
        frame = app.ALARM_FRAME
        self.assertEqual(frame[0], 0xfd)
        self.assertEqual(int.from_bytes(frame[1:3], 'big'), len(frame) - 3)
        self.assertEqual(frame[3:5], b'\x01\x01')
        self.assertEqual(frame[5:-1].decode('gbk'), '[v16]有人跌倒！请注意！')
        checksum = 0
        for byte in frame:
            checksum ^= byte
        self.assertEqual(checksum, 0)

    def test_fragmented_uart_with_logs_and_bad_crc(self):
        reader = app.TelemetryReader()
        wire = b'LIVE_FRAME test\r\n' + packet(1, [0, 1]) + packet(2, [2])
        reports = []
        for byte in wire:
            reports.extend(reader.feed(bytes([byte])))
        self.assertEqual([r['seq'] for r in reports], [1, 2])
        bad = packet(3, [2]).replace(b'"state": 2', b'"state": 1')
        self.assertEqual(reader.feed(bad), [])
        self.assertEqual(reader.bad_frames, 1)
        self.assertEqual(reader.feed(packet(4, [0]))[0]['seq'], 4)

    def test_invalid_protocol_and_states(self):
        reader = app.TelemetryReader()
        for states in [[True], [3], ['2']]:
            self.assertEqual(reader.feed(packet(1, states)), [])
        good = packet(1, [2])
        parts = good.split(b' ', 3)
        parts[1] = b'1'
        self.assertEqual(reader.feed(b' '.join(parts)), [])
        self.assertEqual(reader.feed(packet(1, [0] * 17)), [])

    def test_overlong_line_recovers(self):
        reader = app.TelemetryReader()
        reader.feed(b'x' * (app.MAX_LINE + 1))
        self.assertLessEqual(len(reader.buffer), app.MAX_LINE)
        self.assertEqual(reader.feed(packet(1, [2])), [])
        self.assertEqual(reader.feed(packet(2, [2]))[0]['seq'], 2)

    def test_red_only_repeat_recovery_and_duplicates(self):
        alarm, sent = app.Alarm(), []

        def update(seq, states, now, **extra):
            report = app.decode(packet(seq, states, **extra))
            alarm.update(report, now, sent.append)

        update(0, [0], 0, alarm_latched=1, events=[dict(state=2)])
        update(1, [1], 1)
        self.assertEqual(sent, [])
        update(2, [0, 2], 2)
        self.assertEqual(len(sent), 1)
        update(2, [2], 30)  # Duplicate data cannot cause a repeat.
        self.assertEqual(len(sent), 1)
        update(3, [2], 16.9)
        self.assertEqual(len(sent), 1)
        update(4, [2], 17)
        self.assertEqual(len(sent), 2)
        update(5, [], 18, alarm_latched=1)
        update(6, [2], 19)
        self.assertEqual(len(sent), 3)
        update(0, [2], 20)  # Board restart does not replay on every frame.
        self.assertEqual(len(sent), 3)

    def test_two_port_loop_with_simulated_serial(self):
        writes = []

        class UART:
            def __init__(self, chunks=()):
                self.chunks = list(chunks)
                self.reply = b''
                self.closed = False

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.closed = True

            @property
            def in_waiting(self):
                return len(self.reply)

            def reset_input_buffer(self):
                self.reply = b''

            def read(self, size):
                if self.chunks:
                    return self.chunks.pop(0)
                if self is board:
                    raise KeyboardInterrupt
                reply, self.reply = self.reply, b''
                return reply

            def write(self, frame):
                writes.append(frame)
                self.reply = b'\x41\x4f'
                return len(frame)

            def flush(self):
                pass

        voice = UART()
        board = UART([packet(0, [1]), packet(1, [2]), packet(2, [2]), packet(3, [0])])
        args = SimpleNamespace(voice_port='COM15', board_port='COM7', test=False)
        ports = [SimpleNamespace(device=p, description='simulated') for p in ['COM7', 'COM15']]
        with patch('serial.tools.list_ports.comports', return_value=ports), \
                patch.object(app, 'open_uart', side_effect=[voice, board]) as opened, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            with self.assertRaises(KeyboardInterrupt):
                app.run(args)
        self.assertEqual(opened.call_args_list[0].args, ('COM15', 9600))
        self.assertEqual(opened.call_args_list[1].args, ('COM7', 460800))
        self.assertEqual(writes, [app.ALARM_FRAME])
        self.assertIn('已接收指令', output.getvalue())
        self.assertTrue(voice.closed and board.closed)

    def test_same_port_rejected(self):
        args = SimpleNamespace(voice_port='COM15', board_port='com15', test=False)
        ports = [SimpleNamespace(device='COM15', description='simulated')]
        with patch('serial.tools.list_ports.comports', return_value=ports), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(ValueError):
                app.choose_ports(args)


if __name__ == '__main__':
    unittest.main(verbosity=2)
