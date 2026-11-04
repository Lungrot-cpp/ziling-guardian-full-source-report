"""Persist real primary-target room observations; never synthesize missing history."""
import argparse
from contextlib import closing
from datetime import date, datetime, time as day_time, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import time
import urllib.request
import uuid

BOARD = 'http://192.168.0.2'
ROOMS = ('厨房', '卫生间', '卧室', '客厅')
TZ = timezone(timedelta(hours=8))
DEFAULT_DB = Path(__file__).resolve().parent / 'data/activity.sqlite3'
GAP_MS = 5000


def board_json(endpoint):
    if endpoint not in ('/api/status', '/api/care'):
        raise ValueError('unsupported board endpoint')
    with urllib.request.urlopen(BOARD + endpoint, timeout=3) as response:
        raw = response.read(100001)
    if len(raw) > 100000:
        raise ValueError('board response too large')
    return json.loads(raw)


def observation(status, care):
    uint = lambda n: type(n) is int and 0 <= n < 2**32
    if (status.get('server') != 'riscv-lwip' or not uint(status.get('uptime_ms'))
            or not uint(status.get('frame_id')) or not isinstance(status.get('tracks'), list)):
        raise ValueError('invalid board status')
    layout = json.dumps([[r.get('rect') for r in care.get('rooms', [])],
                         care.get('config', {}).get('corners')], sort_keys=True)
    track, room = care.get('track'), care.get('room')
    xy = [care.get('x'), care.get('y')]
    valid = (care.get('calibrated') is True and care.get('valid') is True
             and uint(care.get('age_ms')) and care['age_ms'] <= 2200
             and uint(track) and track > 0 and type(room) is int and 0 <= room < 4
             and any(t.get('id') == track for t in status['tracks'])
             and all(type(v) in (int, float) and math.isfinite(v) and 0 <= v <= 1 for v in xy))
    return (status['uptime_ms'], status['frame_id'], int(valid), track if valid else None,
            room if valid else None, *(xy if valid else (None, None)),
            hashlib.sha256(layout.encode()).hexdigest())


class Recorder:
    def __init__(self, filename):
        filename = Path(filename)
        filename.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(filename)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('CREATE TABLE IF NOT EXISTS samples ('
                        'at_ms INTEGER, session TEXT, uptime INTEGER, frame INTEGER, '
                        'valid INTEGER, track INTEGER, room INTEGER, x REAL, y REAL, layout TEXT, '
                        'UNIQUE(session,uptime,frame))')
        self.db.execute('CREATE INDEX IF NOT EXISTS sample_time ON samples(at_ms)')
        self.session = uuid.uuid4().hex
        self.previous = None

    def save(self, status, care, now_ms):
        values = observation(status, care)
        # A new session prevents interpolation across board restarts or clock rollback.
        if self.previous and (values[0] < self.previous[0] or values[1] < self.previous[1]
                              or now_ms < self.previous[2]):
            self.session = uuid.uuid4().hex
        with self.db:
            self.db.execute('INSERT OR IGNORE INTO samples VALUES (?,?,?,?,?,?,?,?,?,?)',
                            (now_ms, self.session, *values))
        self.previous = (*values[:2], now_ms)

    def gap(self, now_ms):
        with self.db:
            self.db.execute('INSERT INTO samples VALUES (?,?,NULL,NULL,0,NULL,NULL,NULL,NULL,NULL)',
                            (now_ms, self.session))


def summarize(filename, requested='today', now=None):
    now = now or datetime.now(TZ)
    chosen = now.astimezone(TZ).date() if requested == 'today' else date.fromisoformat(requested)
    if requested != 'today' and chosen.isoformat() != requested:
        raise ValueError('date must be YYYY-MM-DD')
    start = int(datetime.combine(chosen, day_time(), TZ).timestamp() * 1000)
    end = min(start + 86400000, int(now.timestamp() * 1000))
    result = dict(source='真实板卡位置采样记录', date=chosen.isoformat(), timezone='UTC+08:00',
                  selected_primary_only=True, available=False, observed_seconds=0,
                  rooms=[], not_observed=list(ROOMS),
                  note='仅统计采集期间选定主目标；目标ID不是身份识别。未记录到不等于没有去过；'
                       '断网、未标定、遮挡和停机时段不能推断活动；不能据此判断食欲、疾病或受伤。')
    filename = Path(filename)
    if not filename.is_file() or end <= start:
        return result
    with closing(sqlite3.connect(filename.resolve().as_uri() + '?mode=ro', uri=True)) as db:
        rows = db.execute('SELECT at_ms,session,uptime,frame,valid,track,room,layout FROM samples '
                          'WHERE at_ms>=? AND at_ms<=? ORDER BY at_ms,rowid',
                          (start - GAP_MS, end)).fetchall()
    seconds = [0.0] * 4
    visits = [0] * 4
    tracks, layouts = set(), set()
    first = last = None
    previous = None
    for row in rows:
        stamp, session, uptime, frame, valid, track, room, layout = row
        continuous = (previous is not None and valid and previous[4]
                      and session == previous[1] and track == previous[5] and layout == previous[7]
                      and 0 < stamp - previous[0] <= GAP_MS
                      and 0 < uptime - previous[2] <= GAP_MS and frame != previous[3])
        if continuous:
            duration = max(0, min(stamp, end) - max(previous[0], start)) / 1000
            seconds[previous[6]] += duration
        if valid and start <= stamp <= end:
            visits[room] += int(not continuous or room != previous[6] or previous[0] < start)
            tracks.add((session, track)); layouts.add(layout)
            first = stamp if first is None else first
            last = stamp
        previous = row
    result.update(available=first is not None, observed_seconds=round(sum(seconds), 3),
                  first_observed_at=datetime.fromtimestamp(first / 1000, TZ).isoformat() if first else None,
                  last_observed_at=datetime.fromtimestamp(last / 1000, TZ).isoformat() if last else None,
                  primary_track_segments=len(tracks), layout_versions=len(layouts),
                  rooms=[dict(name=name, visits=visits[i], observed_seconds=round(seconds[i], 3))
                         for i, name in enumerate(ROOMS) if visits[i] or seconds[i]],
                  not_observed=[name for i, name in enumerate(ROOMS) if not visits[i] and not seconds[i]])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', type=Path, default=DEFAULT_DB)
    parser.add_argument('--summary', metavar='YYYY-MM-DD_OR_today')
    args = parser.parse_args()
    if args.summary:
        print(json.dumps(summarize(args.db, args.summary), ensure_ascii=False, indent=2))
        return
    recorder = Recorder(args.db)
    print('记录选定主目标的真实房间采样；停止采集按 Ctrl+C。', flush=True)
    try:
        while True:
            try:
                status = board_json('/api/status')
                care = board_json('/api/care')
                recorder.save(status, care, int(time.time() * 1000))
            except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
                recorder.gap(int(time.time() * 1000))
                print('本次未取得有效位置：' + type(exc).__name__, flush=True)
            time.sleep(2)
    except KeyboardInterrupt:
        pass
    finally:
        recorder.db.close()


if __name__ == '__main__':
    main()
