"""Pack the verified pair; back up, write and read back the existing V6A1 layout."""
from pathlib import Path
import hashlib, json, os, re, runpy, struct, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'flash'
FPGA = ROOT / 'recognition/fpga'
SW = ROOT / 'recognition/software'
EFINITY = Path('D:/Efinity/2026.1')
VENDOR = EFINITY / 'pgm/bin/ftdi_pgm.bat'
IMAGE = OUT / 'v12_network_video_autoboot.hex'
sha = lambda data: hashlib.sha256(data).hexdigest()

env = os.environ.copy()
for name, value in {'EFINITY_HOME': EFINITY, 'EFXPGM_HOME': EFINITY/'pgm',
                    'EFXDBG_HOME': EFINITY/'debugger', 'EFXSVF_HOME': EFINITY/'debugger/svf_player',
                    'PYTHONHOME': EFINITY/'python311',
                    'EFINITY_USER_DIR_INI': Path(os.environ['LOCALAPPDATA'])/'efinity/user_dir.ini'}.items():
    env[name] = str(value)

def save(name, value):
    (OUT/name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def run(args, name):
    print(name, flush=True)
    with (OUT/name).open('w', encoding='utf-8') as log:
        subprocess.run(['cmd.exe', '/d', '/c', str(VENDOR), *args], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    return (OUT/name).read_text(encoding='utf-8', errors='replace')

def command(mode, *args):
    scan = (OUT/'usb_scan.log').read_text(encoding='utf-8')
    urls = set(re.findall(r'url\[1\]:\s*(ftdi://\S+)', scan))
    assert len(urls) == 1, 'Expected exactly one board'
    return ['--mode', mode, '--url', urls.pop(), '--board_profile',
            'Generic Board Profile Using FT4232H', '--jtag_clock_freq',
            '800000' if mode == 'jtag' else '6000000', *map(str,args)]

def prepared():
    manifest = json.loads((OUT/'manifest.json').read_text(encoding='utf-8'))
    data = bytes.fromhex(IMAGE.read_text(encoding='ascii'))
    assert sha(IMAGE.read_bytes()) == manifest['image_hex_sha256']
    assert sha(data) == manifest['image_raw_sha256']
    assert sha((FPGA/'outflow/v6_person.bit').read_bytes()) == manifest['bit_sha256']
    assert sha((SW/'build-accel/personInferenceDemo.bin').read_bytes()) == manifest['bin_sha256']
    return manifest, data

stage = sys.argv[1]
if stage == 'prepare':
    receipt = json.loads((FPGA/'outflow/build_receipt.json').read_text())
    assert receipt['inputs'] == runpy.run_path(str(FPGA/'build.py'))['input_hashes']()
    validation = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT/'validation/claude-fonts-result.json'
    result = json.loads(validation.read_text(encoding='utf-8'))
    assert result['offline_passed']
    assert sha((FPGA/'outflow/v6_person.bit').read_bytes()) == receipt['bit_sha256'] == result['bit_sha256']
    sw_receipt = json.loads((SW/'build-accel/build_receipt.json').read_text())
    for name, expected in sw_receipt['inputs'].items():
        assert sha((SW/name).read_bytes()) == expected, name
    assert sha((SW/'build-accel/personInferenceDemo.elf').read_bytes()) == sw_receipt['elf_sha256'] == result['elf_sha256']
    subprocess.run(['D:/toolchain/bin/riscv-none-elf-objcopy.exe', '-O', 'binary',
                    str(SW/'build-accel/personInferenceDemo.elf'), str(OUT/'elf-payload.bin')], check=True)
    app = (SW/'build-accel/personInferenceDemo.bin').read_bytes()
    assert app == (OUT/'elf-payload.bin').read_bytes()
    lanes = [bytes(int(v,2) for v in (FPGA/'ip/sapphire_soc'/
             f'EfxSapphireSoc.v_toplevel_system_ramA_logic_ram_symbol{i}.bin').read_text().split()) for i in range(4)]
    rom = bytes(lanes[i%4][i//4] for i in range(4096))
    previous_boot = ROOT/'bootloader/build/boot.bin'
    assert rom == previous_boot.read_bytes().ljust(4096,b'\0'), 'Boot ROM does not match known variable-length V6A1 loader'
    raw = bytes.fromhex((FPGA/'outflow/v6_person.hex').read_text(encoding='ascii'))
    assert len(raw) <= 0x37fff0 and 0 < len(app) <= 0x480000
    header = b'V6A1' + struct.pack('<I',len(app)) + b'\xff'*8
    image = raw + b'\xff'*(0x37fff0-len(raw)) + header + app
    assert len(image) <= 0x800000 and image[0x380000:] == app
    assert image[0x37fff0:0x380000] == header
    IMAGE.write_text(''.join(f'{b:02X}\n' for b in image), encoding='ascii', newline='\n')
    assert bytes.fromhex(IMAGE.read_text()) == image
    save('manifest.json', dict(version=ROOT.name, image_bytes=len(image), image_raw_sha256=sha(image),
         image_hex_sha256=sha(IMAGE.read_bytes()), fpga_bytes=len(raw), fpga_address=0,
         header_address=0x37fff0, application_address=0x380000, application_bytes=len(app),
         bit_sha256=result['bit_sha256'], elf_sha256=result['elf_sha256'], bin_sha256=sha(app),
         boot_rom_sha256=sha(rom)))
    print('FLASH_IMAGE_PREPARED', len(image), flush=True)
elif stage == 'backup':
    prepared()
    assert not (OUT/'previous_flash_8MiB.hex').exists(), 'Do not overwrite the previous Flash backup'
    run(['--list_usb'], 'usb_scan.log')
    assert 'finished with JTAG programming' in run(command('jtag', EFINITY/'pgm/fli/titanium/u10660A79.bit'), 'bridge.log')
    log = run(command('jtag_bridge', '--jtag_bridge_mode', 'read', '--address', '0',
              '--num_bytes', '8388608', '--output_file', OUT/'previous_flash_8MiB.hex'), 'backup.log')
    assert '0xEF6017' in log and '8 MiB' in log and 'Finished read' in log
    backup = bytes.fromhex((OUT/'previous_flash_8MiB.hex').read_text())
    assert len(backup) == 8388608
    (OUT/'previous_flash_8MiB.bin').write_bytes(backup)
    save('backup.json', dict(bytes=len(backup), sha256=sha(backup)))
    print('FULL_FLASH_BACKUP_PASS',sha(backup),flush=True)
elif stage == 'program':
    manifest, expected = prepared()
    backup = (OUT/'previous_flash_8MiB.bin').read_bytes()
    assert len(backup) == 8388608 and sha(backup) == json.loads((OUT/'backup.json').read_text())['sha256']
    run(['--list_usb'], 'usb_scan.log')
    assert 'finished with JTAG programming' in run(command('jtag', EFINITY/'pgm/fli/titanium/u10660A79.bit'), 'bridge.log')
    log = run(command('jtag_bridge','--jtag_bridge_mode','all','--address','0',
                     '--verify_method','hostx1',IMAGE),'program.log')
    assert 'Flash verify successful' in log and 'JTAG2SPI programming...done' in log
    run(command('jtag_bridge','--jtag_bridge_mode','read','--address','0',
                '--num_bytes',len(expected),'--output_file',OUT/'programmed_range_readback.hex'),'readback.log')
    actual = bytes.fromhex((OUT/'programmed_range_readback.hex').read_text())
    assert actual == expected
    save('flash-validation.json',dict(flash_written=True,flash_verify=True,
         verify_method='hostx1 and full programmed range byte comparison',compared_bytes=len(actual),
         readback_sha256=sha(actual),image=manifest,backup_sha256=sha(backup),
         rom_boot_verified=False,physical_power_cycle_confirmed=False))
    print('FLASH_PROGRAM_AND_FULL_READBACK_PASS',len(actual),flush=True)
elif stage == 'boot':
    import serial, threading, time, urllib.request, zlib
    prepared()
    status = json.loads((OUT/'flash-validation.json').read_text(encoding='utf-8'))
    assert status['flash_verify']
    stop = threading.Event()
    with serial.Serial('COM7', 460800, timeout=.1) as port:
        def capture():
            with (OUT/'boot-from-flash-uart.log').open('wb') as log:
                while not stop.is_set():
                    data = port.read(8192)
                    if data:
                        log.write(data)
                        log.flush()
        reader = threading.Thread(target=capture)
        reader.start()
        try:
            log = run(command('jtag',FPGA/'outflow/v6_person.bit'),'reconfigure-only.log')
            assert 'finished with JTAG programming' in log
            stop.wait(40)
        finally:
            stop.set()
            reader.join()
    raw = (OUT/'boot-from-flash-uart.log').read_bytes()
    assert b'RESULT_CHECK=PASS' in raw and b'V6_DIAGNOSTICS detection_chain=1' in raw
    assert b'ONBOARD_HTTP_READY' in raw and b'TRAP' not in raw
    rows = []
    for line in raw[raw.rfind(b'@V6BOOT'):].split(b'\n')[:-1]:
        line = line.rstrip(b'\r')
        if not line.startswith(b'@V6 '):
            continue
        _, length, crc, payload = line.split(b' ',3)
        assert len(payload) == int(length) and zlib.crc32(payload) == int(crc,16)
        rows.append(json.loads(payload))
    assert len(rows) >= 5 and rows[-1]['detection_seq'] > rows[0]['detection_seq']
    save('boot-packets.json',rows)
    op = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    http = {}
    for path in ['/api/status','/api/power','/api/report']:
        with op.open('http://192.168.0.2'+path,timeout=12) as response:
            http[path] = json.loads(response.read())
    assert http['/api/status']['server'] == 'riscv-lwip'
    assert 'automatic' in http['/api/power'] and http['/api/report']['capacity'] == 16
    save('boot-http.json',http)
    status.update(rom_boot_verified=True,application_downloaded_by_jtag=False,
        boot_method='FPGA-only JTAG configuration; existing ROM reads application from SPI Flash',
        startup_reference_pass=True,telemetry_packets=len(rows),
        detection_seq=[rows[0]['detection_seq'],rows[-1]['detection_seq']],
        http_new_power_report_endpoints=True,physical_power_cycle_confirmed=False)
    save('flash-validation.json',status)
    print('ROM_FLASH_STARTUP_PASS',len(rows),flush=True)
else:
    raise ValueError('Expected prepare, backup, program, or boot')
