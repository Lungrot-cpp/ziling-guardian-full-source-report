"""Read the board's power-policy counters; never load firmware or switch modes."""
import argparse
import csv
from datetime import datetime
import json
from pathlib import Path
import time
from urllib.error import HTTPError, URLError
from urllib.request import urlopen

URL = 'http://192.168.0.2/api/power'
ROOT = Path(__file__).resolve().parents[1]
MODES = {'active': '正常检测', 'watch': '轻量监视', 'waking': '恢复检测'}
FIELDS = ['电脑时间', '板端时间_ms', '自动管理', '模式', '最近唤醒', '无人时间_ms',
          '累计推理次数', '累计推理耗时_us', '累计运动检查', '有效运动检查',
          '传感器正常', '告警或救援阻止轻量', '累计轻量时长_ms',
          '采样间隔_ms', '推理次数每秒', '推理耗时占比_pct', '实际功率_W']


def read_sample():
    with urlopen(URL, timeout=3) as response:
        p = json.load(response)
    if not isinstance(p, dict) or type(p.get('mode')) is not str or p['mode'] not in MODES:
        raise ValueError('板卡没有返回有效的轻量监视数据，请确认已加载新版软件。')
    for key in ('sample_ms', 'inferences', 'inference_us', 'motion_checks',
                'motion_valid_checks', 'empty_ms'):
        if type(p.get(key)) is not int or p[key] < 0:
            raise ValueError('板卡计数不完整：' + key)
    if any(p.get(key) not in (0, 1) for key in ('automatic', 'monitor_ok', 'event_blocked')):
        raise ValueError('板卡状态字段无效。')
    if p.get('wake_reason') not in ('boot', 'motion', 'person', 'fault', 'manual'):
        raise ValueError('板卡唤醒原因无效。')
    residence = p.get('residence_ms')
    if not isinstance(residence, list) or len(residence) != 3 or any(type(n) is not int or n < 0 for n in residence):
        raise ValueError('板卡模式时长无效。')
    return p


def interval(previous, current):
    if previous is None or any(previous[k] != current[k] for k in ('mode', 'automatic')):
        return None
    ms = current['sample_ms'] - previous['sample_ms']
    count = current['inferences'] - previous['inferences']
    busy = current['inference_us'] - previous['inference_us']
    if ms <= 0 or count < 0 or busy < 0:
        return None
    return ms, count, busy


def record(seconds):
    # Fail before creating a results folder when the board/new endpoint is unavailable.
    first = read_sample()
    folder = ROOT / '记录' / ('低功耗_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
    folder.mkdir(parents=True)
    groups = {}
    summary = dict(来源=URL, 开始时间=datetime.now().isoformat(timespec='seconds'),
                   计划记录秒数=seconds, 有效采样数=0, 请求失败次数=0,
                   实际功率已测量=False, 结束原因='记录完成', 各模式统计=[])
    print('正在记录：', folder, flush=True)
    print('默认记录3分钟；按 Ctrl+C 可提前结束并保存。请在网页手动切换模式。', flush=True)
    print('本工具不切换模式、不下载程序、不写Flash；实际功率保持空白。', flush=True)
    previous = None
    started = time.monotonic()
    failures = 0
    with (folder / '原始记录.csv').open('w', encoding='utf-8-sig', newline='') as csv_file, \
         (folder / '原始数据.jsonl').open('w', encoding='utf-8') as raw:
        writer = csv.writer(csv_file)
        writer.writerow(FIELDS)
        try:
            while True:
                try:
                    p = first if first is not None else read_sample()
                    first = None
                    failures = 0
                except (URLError, TimeoutError, OSError, ValueError) as error:
                    summary['请求失败次数'] += 1
                    failures += 1
                    previous = None
                    print('未取得新数据：', error, flush=True)
                    if failures >= 3:
                        summary['结束原因'] = '连续3次请求失败，记录中断'
                        break
                    if time.monotonic() - started >= seconds:
                        summary['结束原因'] = '到时结束，末段连接异常'
                        break
                    time.sleep(1)
                    continue
                now = datetime.now().isoformat(timespec='milliseconds')
                raw.write(json.dumps(dict(电脑时间=now, 板端数据=p), ensure_ascii=False) + '\n')
                raw.flush()
                delta = interval(previous, p)
                metrics = ['', '', '']
                if delta:
                    ms, count, busy = delta
                    metrics = [ms, round(count * 1000 / ms, 4), round(busy / ms / 10, 4)]
                    key = (p['automatic'], p['mode'])
                    g = groups.setdefault(key, dict(自动管理=bool(key[0]), 模式=MODES[key[1]],
                        有效观察毫秒=0, 推理次数=0, 推理耗时微秒=0))
                    g['有效观察毫秒'] += ms
                    g['推理次数'] += count
                    g['推理耗时微秒'] += busy
                writer.writerow([now, p['sample_ms'], p['automatic'], MODES[p['mode']],
                    p['wake_reason'], p['empty_ms'], p['inferences'], p['inference_us'],
                    p['motion_checks'], p['motion_valid_checks'], p['monitor_ok'], p['event_blocked'],
                    p['residence_ms'][1], *metrics, ''])
                csv_file.flush()
                summary['有效采样数'] += 1
                print(f"{now[11:19]}  {'自动' if p['automatic'] else '连续对照'} / {MODES[p['mode']]}"
                      f"  累计推理 {p['inferences']} 次  本段 {metrics[1] if delta else '--'} 次/秒", flush=True)
                # Duplicate samples do not replace the basis of the next interval.
                if previous is None or p['sample_ms'] != previous['sample_ms'] or p['automatic'] != previous['automatic'] or p['mode'] != previous['mode']:
                    previous = p
                if time.monotonic() - started >= seconds:
                    break
                time.sleep(min(1, max(0, seconds - (time.monotonic() - started))))
        except KeyboardInterrupt:
            summary['结束原因'] = '用户按Ctrl+C结束'
        finally:
            for g in groups.values():
                ms = g['有效观察毫秒']
                g['平均推理次数每秒'] = round(g['推理次数'] * 1000 / ms, 4)
                g['推理耗时占比_pct'] = round(g['推理耗时微秒'] / ms / 10, 4)
            summary['各模式统计'] = list(groups.values())
            summary['结束时间'] = datetime.now().isoformat(timespec='seconds')
            summary['说明'] = '跨模式、重启、计数回退及断流区间不参与均值；推理耗时占比不是CPU总占用率或节电率，实际功率未测量。'
            (folder / '统计汇总.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
            with (folder / '统计汇总.csv').open('w', encoding='utf-8-sig', newline='') as f:
                fields = ['自动管理', '模式', '有效观察毫秒', '推理次数', '推理耗时微秒', '平均推理次数每秒', '推理耗时占比_pct']
                w = csv.DictWriter(f, fieldnames=fields)
                w.writeheader()
                w.writerows(groups.values())
            print('已保存记录和统计汇总：', folder, flush=True)
    return folder, summary


def main():
    parser = argparse.ArgumentParser(description='记录板卡轻量监视计数；不会切换模式或下载固件。')
    parser.add_argument('--seconds', type=int, default=180, help='记录秒数，默认180')
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('记录秒数必须大于0')
    try:
        _, summary = record(args.seconds)
        return 1 if '异常' in summary['结束原因'] or '中断' in summary['结束原因'] else 0
    except HTTPError as error:
        print('无法读取记录接口：', error)
        if error.code == 404:
            print('板卡可能仍是旧软件，请先加载已合入轻量监视的新版ELF。')
    except (URLError, TimeoutError, OSError, ValueError) as error:
        print('无法开始记录：', error)
        print('请确认网线和板卡地址192.168.0.2，并确认已加载新版软件。')
    except KeyboardInterrupt:
        print('已取消开始记录。')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
