"""Recorder checks with fake samples only; no board or physical serial access."""
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError
import record_low_power as recorder


def sample(ms=0, count=0, busy=0, mode='active', automatic=0):
    return dict(sample_ms=ms, inferences=count, inference_us=busy, mode=mode,
                automatic=automatic, wake_reason='manual', monitor_ok=1, event_blocked=0,
                motion_checks=0, motion_valid_checks=0, empty_ms=0,
                residence_ms=[0, ms if mode == 'watch' else 0, 0], power_watts=None)


class RecorderTests(unittest.TestCase):
    def test_rates_and_cut_boundaries(self):
        a, b = sample(1000, 2, 200000), sample(6000, 12, 1200000)
        self.assertEqual(recorder.interval(a, b), (5000, 10, 1000000))
        self.assertIsNone(recorder.interval(None, b))
        self.assertIsNone(recorder.interval(a, a))
        self.assertIsNone(recorder.interval(b, a))
        self.assertIsNone(recorder.interval(a, sample(6000, 12, 1200000, 'watch')))
        self.assertIsNone(recorder.interval(a, sample(6000, 12, 1200000, automatic=1)))
        self.assertIsNone(recorder.interval(a, sample(6000, 1, 1200000)))

    def test_schema(self):
        with patch.object(recorder, 'urlopen', return_value=io.BytesIO(json.dumps(sample()).encode())):
            self.assertEqual(recorder.read_sample(), sample())
        for changed in ({'mode': []}, {'sample_ms': -1}, {'inference_us': '10'},
                        {'wake_reason': '=1+2'}, {'residence_ms': [1]}, {'monitor_ok': 2}):
            value = {**sample(), **changed}
            with self.subTest(changed=changed), patch.object(recorder, 'urlopen', return_value=io.BytesIO(json.dumps(value).encode())):
                with self.assertRaises(ValueError):
                    recorder.read_sample()

    def run_record(self, folder, samples, seconds):
        clock = [0.0]
        def advance(value):
            clock[0] += value
        with patch.object(recorder, 'ROOT', Path(folder)), \
             patch.object(recorder, 'read_sample', side_effect=samples), \
             patch.object(recorder.time, 'monotonic', side_effect=lambda: clock[0]), \
             patch.object(recorder.time, 'sleep', side_effect=advance), \
             patch('builtins.print'):
            return recorder.record(seconds)

    def test_csv_and_separate_mode_summary(self):
        values = [sample(0, 0, 0), sample(1000, 2, 200000),
                  sample(2000, 3, 300000, 'watch', 1),
                  sample(3000, 3, 300000, 'watch', 1), sample(4000, 4, 400000, 'watch', 1)]
        with tempfile.TemporaryDirectory() as temp:
            folder, summary = self.run_record(temp, values, 4)
            self.assertEqual(summary['有效采样数'], 5)
            groups = summary['各模式统计']
            self.assertEqual(groups[0]['平均推理次数每秒'], 2)
            self.assertEqual(groups[0]['推理耗时占比_pct'], 20)
            self.assertEqual(groups[1]['平均推理次数每秒'], .5)
            self.assertEqual(groups[1]['推理耗时占比_pct'], 5)
            with (folder/'原始记录.csv').open(encoding='utf-8-sig', newline='') as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 5)
            self.assertTrue(all(r['实际功率_W'] == '' for r in rows))
            self.assertEqual(rows[2]['推理次数每秒'], '')
            self.assertEqual(len((folder/'原始数据.jsonl').read_text(encoding='utf-8').splitlines()), 5)
            self.assertEqual(json.loads((folder/'统计汇总.json').read_text(encoding='utf-8'))['各模式统计'], groups)

    def test_disconnect_saves_partial(self):
        with tempfile.TemporaryDirectory() as temp:
            folder, summary = self.run_record(temp, [sample(), URLError('offline'), URLError('offline'), URLError('offline')], 10)
            self.assertEqual(summary['有效采样数'], 1)
            self.assertEqual(summary['请求失败次数'], 3)
            self.assertIn('中断', summary['结束原因'])
            self.assertTrue((folder/'统计汇总.csv').exists())

    def test_gap_not_counted(self):
        with tempfile.TemporaryDirectory() as temp:
            _, summary = self.run_record(temp, [sample(), URLError('gap'), sample(2000, 4, 400000), sample(3000, 6, 600000)], 3)
            g = summary['各模式统计'][0]
            self.assertEqual(g['有效观察毫秒'], 1000)
            self.assertEqual(g['推理次数'], 2)

    def test_keyboard_interrupt_saves(self):
        with tempfile.TemporaryDirectory() as temp:
            folder, summary = self.run_record(temp, [sample(), KeyboardInterrupt()], 10)
            self.assertIn('Ctrl+C', summary['结束原因'])
            self.assertTrue((folder/'统计汇总.json').exists())

    def test_old_firmware_no_empty_result_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(HTTPError):
                self.run_record(temp, [HTTPError(recorder.URL, 404, 'old firmware', {}, None)], 3)
            self.assertEqual(list(Path(temp).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
