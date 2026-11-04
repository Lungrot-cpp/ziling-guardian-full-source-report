from datetime import datetime, timedelta
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import activity_history as a


class ActivityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'activity.sqlite3'
        self.rec = a.Recorder(self.path)
        self.addCleanup(self.rec.db.close)
        self.at = datetime(2026, 11, 3, 12, tzinfo=a.TZ)
        self.base = int(self.at.timestamp() * 1000)

    def save(self, delta, room=2, track=7, valid=True, uptime=None):
        status = dict(server='riscv-lwip', uptime_ms=delta+1000 if uptime is None else uptime,
                      frame_id=delta//100+1, tracks=[{'id':track}])
        care = dict(calibrated=True, valid=valid, age_ms=10, track=track, room=room, x=.2, y=.7)
        self.rec.save(status, care, self.base+delta)

    def summary(self):
        return a.summarize(self.path, '2026-11-03', self.at+timedelta(hours=1))

    def test_real_room_summary_and_missing_history(self):
        self.assertFalse(a.summarize(self.path.with_name('missing.db'))['available'])
        self.save(0); self.save(2000); self.save(4000, room=3); self.save(6000, room=3)
        s = self.summary()
        self.assertEqual(s['observed_seconds'], 6)
        self.assertEqual([r['name'] for r in s['rooms']], ['卧室','客厅'])
        self.assertEqual(s['not_observed'], ['厨房','卫生间'])
        self.assertIn('不等于没有去过', s['note'])

    def test_gap_identity_and_reboot_never_interpolated(self):
        self.save(0); self.save(2000)
        self.rec.gap(self.base+4000)
        self.save(6000); self.save(8000, track=8); self.save(10000, track=8, uptime=100)
        self.assertEqual(self.summary()['observed_seconds'], 2)
        self.assertEqual(self.summary()['primary_track_segments'], 3)

    def test_long_gap_stale_and_duplicate(self):
        self.save(0); self.save(2000); self.save(2000)
        self.save(20000); self.save(22000, valid=False); self.save(24000)
        self.assertEqual(self.summary()['observed_seconds'], 2)
        self.assertEqual(self.rec.db.execute('SELECT count(*) FROM samples').fetchone()[0], 5)

    def test_midnight_clips_dwell(self):
        self.base = int(datetime(2026,11,3,tzinfo=a.TZ).timestamp()*1000)-1000
        self.save(0); self.save(2000)
        self.assertEqual(self.summary()['observed_seconds'], 1)

    def test_invalid_location_and_dates(self):
        s=dict(server='riscv-lwip',uptime_ms=1,frame_id=1,tracks=[{'id':7}])
        c=dict(calibrated=False,valid=True,age_ms=0,track=7,room=0,x=.2,y=.2)
        self.assertFalse(a.observation(s,c)[2])
        c.update(calibrated=True,x=float('nan'))
        self.assertFalse(a.observation(s,c)[2])
        for day in ['../../config','20261103','2026-13-01']:
            with self.assertRaises(ValueError): a.summarize(self.path,day)

    def test_lamp_changes_preserve_layout_but_calibration_changes_do_not(self):
        status=dict(server='riscv-lwip',uptime_ms=1,frame_id=1,tracks=[{'id':7}])
        care=dict(calibrated=True,valid=True,age_ms=0,track=7,room=0,x=.2,y=.2,
                  rooms=[dict(rect=[0,0,.5,.5],lit=True)],config=dict(corners=[0,0,1,0,1,1,0,1]))
        first=a.observation(status,care)[-1]
        care['rooms'][0]['lit']=False
        self.assertEqual(first,a.observation(status,care)[-1])
        care['config']['corners'][0]=.1
        self.assertNotEqual(first,a.observation(status,care)[-1])

    def test_live_mcp_uses_database_without_network_or_simulation(self):
        self.save(0); self.save(2000)
        request=dict(jsonrpc='2.0',id=1,method='tools/call',
                     params=dict(name='board_activity_summary',arguments={'date':'2026-11-03'}))
        result=subprocess.run([sys.executable,str(a.DEFAULT_DB.parent.parent/'assistant.py'),
                               '--mcp','--live','--activity-db',str(self.path)],
                              input=json.dumps(request)+'\n',text=True,encoding='utf-8',
                              capture_output=True,timeout=10,check=True)
        content=json.loads(result.stdout)['result']['content'][0]['text']
        self.assertTrue(json.loads(content)['available'])
        self.assertNotIn('合成示意图',content)


if __name__ == '__main__':
    unittest.main()
