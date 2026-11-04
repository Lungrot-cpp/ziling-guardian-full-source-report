"""Protocol and CLI dry-run checks without accessing any device."""
from pathlib import Path
import re,subprocess,sys
from test_voice_module import frame_for
header=(Path(__file__).resolve().parents[1]/'recognition/software/src/voice_messages.h').read_text()
texts={'voice_boot':'[v8][t5]系统已启动。','voice_yellow':'[v8][t5]检测到疑似跌倒，请注意查看。',
       'voice_red':'[v10][t5]soundq，检测到异常状态，请立即查看。'}
for name,text in texts.items():
    body=re.search(r'\b'+name+r'\[\]=\{([^}]+)\}',header)[1]
    assert frame_for(text)==bytes(int(value,16) for value in body.split(','))
assert len(frame_for('中'*100))==206
for text in ('','中'*101,'😀'):
    try:frame_for(text)
    except (UnicodeEncodeError,ValueError):pass
    else:raise AssertionError('invalid text accepted')
subprocess.run([sys.executable,str(Path(__file__).with_name('test_voice_module.py')),'--dry-run'],check=True)
print('MODULE_FRAMES_PASS: exact firmware match, max length, invalid length/encoding, no-port dry run')
