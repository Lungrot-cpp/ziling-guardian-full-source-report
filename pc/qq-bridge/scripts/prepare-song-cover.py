"""Run with D:/qqbrge/singing/.venv/Scripts/python.exe; input must contain actual singing."""
import os
os.environ['MKL_THREADING_LAYER'] = 'SEQUENTIAL'
os.environ['HF_HUB_DISABLE_XET'] = '1'
os.environ['PYTHONUTF8'] = '1'

import argparse
import json
import subprocess
import sys
import uuid
from pathlib import Path
import numpy as np
import soundfile as sf
from natural_cover_mix import mix_natural_vocal

parser = argparse.ArgumentParser(description='Separate a song, convert its singing voice, then restore its backing track.')
parser.add_argument('source', type=Path)
parser.add_argument('filename', help='Output WAV or MP3 filename in the QQ audio library')
parser.add_argument('--voice', choices=['rourou', 'yujie', 'original'], default='rourou')
parser.add_argument('--stems-dir', type=Path, help='Reuse the existing separated vocal and backing stems')
parser.add_argument('--replace', action='store_true', help='Replace a cover only after the new file passes validation')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
ffmpeg = json.loads((root / 'config.json').read_text(encoding='utf-8-sig'))['socialV2']['voice']['ffmpegPath']
os.environ['PATH'] = str(Path(ffmpeg).parent) + os.pathsep + os.environ['PATH']
singing = Path(r'D:\qqbrge\singing')
source = args.source.resolve(strict=True)
info = sf.info(source)
if not 1 <= info.duration <= 360:
    parser.error('Source must contain 1–360 seconds of actual singing.')
if Path(args.filename).name != args.filename or Path(args.filename).suffix.lower() not in {'.wav', '.mp3'}:
    parser.error('Use a WAV or MP3 filename without directories.')
output = root / 'audio' / args.filename
if output.exists() and not args.replace:
    parser.error('Output already exists; choose a new filename.')
job = root / 'state/cover-jobs' / uuid.uuid4().hex
job.mkdir(parents=True)
# A separate process releases its CUDA context before voice conversion on the 8 GB GPU.
if args.stems_dir is None:
    subprocess.run([sys.executable, '-u', '-c',
                'import torch; from audio_separator.utils.cli import main; main()',
                str(source), '--model_filename', 'UVR-MDX-NET-Inst_HQ_3.onnx',
                '--model_file_dir', str(singing / 'separation-models'),
                '--output_dir', str(job), '--output_format', 'WAV', '--use_soundfile'],
                   check=True, timeout=600)
stems = [p for p in (args.stems_dir.resolve(strict=True) if args.stems_dir else job).glob('*.wav')
         if '_(Vocals)_' in p.name or '_(Instrumental)_' in p.name]
assert len(stems) == 2, stems
vocals = next(p for p in stems if '_(Vocals)_' in p.name)
backing = next(p for p in stems if '_(Instrumental)_' in p.name)
if args.voice == 'rourou':
    (job / 'converted').mkdir()
    subprocess.run([sys.executable, '-u', '-c',
                    'import runpy,torch; torch.set_num_threads(4); torch.manual_seed(42); runpy.run_path("rvc/infer/infer.py",run_name="__main__")',
                    '--input-path', str(vocals), '--output-path', str(job / 'converted/rourou.wav'),
                    '--pth-path', str(singing / 'applio/rvc/models/voices/rourou/rourou.pth'),
                    '--pitch', '0', '--f0-method', 'rmvpe', '--index-rate', '0',
                    '--volume-envelope', '0.25', '--protect', '0.33'],
                   cwd=singing / 'applio', check=True, timeout=1800)
else:
    reference = root / 'assets' / ('voice-yujie.wav' if args.voice == 'yujie' else 'voice-reference.wav')
    subprocess.run([sys.executable, '-u', 'inference.py', '--source', str(vocals),
                    '--target', str(reference), '--output', str(job / 'converted'),
                    '--diffusion-steps', '80', '--inference-cfg-rate', '0.4', '--f0-condition', 'True',
                    '--auto-f0-adjust', 'False', '--fp16', 'False'],
                   cwd=singing / 'seed-vc-main', check=True, timeout=1800)
converted = next((job / 'converted').glob('*.wav'))
voice, rate = sf.read(converted, always_2d=True)
assert np.isfinite(voice).all() and np.sqrt(np.mean(voice ** 2)) > .001
music, music_rate = sf.read(backing, always_2d=True)
if args.voice == 'rourou':
    import soxr
    voice = soxr.resample(voice, rate, music_rate)
    rate = music_rate
assert rate == music_rate == 44100 and abs(len(voice) - len(music)) / rate < .1
length = min(len(voice), len(music))
if args.voice != 'rourou':
    source_voice, source_rate = sf.read(vocals, always_2d=True)
    assert source_rate == rate and len(source_voice) >= length
    pitch = np.load(job / 'converted/source-f0.npy')
    voice = mix_natural_vocal(source_voice[:length], voice[:length], pitch, rate)
mixed = voice[:length] + music[:length]
assert np.isfinite(mixed).all() and np.sqrt(np.mean(mixed ** 2)) > .001
mixed *= min(1, .98 / max(float(np.max(np.abs(mixed))), .001))
output.parent.mkdir(exist_ok=True)
pending = output.with_suffix(output.suffix + '.pending')
if output.suffix.lower() == '.mp3':
    sf.write(pending, mixed, rate, format='MP3', subtype='MPEG_LAYER_III',
             bitrate_mode='CONSTANT', compression_level=.5)
else:
    sf.write(pending, mixed, rate, format='WAV', subtype='PCM_16')
assert 10000 < pending.stat().st_size < 20 * 1024 * 1024
assert abs(sf.info(pending).duration - length / rate) < .1
pending.replace(output)
print('COVER_READY', output, 'seconds=', round(length / rate, 3))
