"""Verify every shipped file; read-only, no board or network access."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent
manifest=json.loads((root/'verification/files.sha256.json').read_text(encoding='utf-8'))
for name,expected in manifest['files'].items():
    p=root/name
    assert p.is_file(), 'Missing file: '+name
    assert hashlib.sha256(p.read_bytes()).hexdigest()==expected, 'Changed file: '+name
print('SUBMISSION_FILES_PASS',len(manifest['files']))
