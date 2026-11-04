from pathlib import Path
import binascii
import json
import os
import struct
import subprocess
import sys

root=Path(__file__).resolve().parents[1];out=root.parent/'validation'
env=os.environ.copy();env['PATH']='D:/fpga/tools/ucrt64/bin;'+env.get('PATH','')
full='--full' in sys.argv
parameters=['-Ptb_network_video.WIDTH=1920','-Ptb_network_video.HEIGHT=1080','-Ptb_network_video.FULL=1'] if full else []
subprocess.run(['D:/fpga/tools/ucrt64/bin/iverilog.exe','-g2012','-s','tb_network_video','-o',str(out/'network-video.vvp'),
                *parameters,'tests/tb_network_video.sv','fpga/rtl/network_packet.sv','fpga/rtl/video_udp.sv'],cwd=root,env=env,check=True)
p=subprocess.run(['D:/fpga/tools/ucrt64/bin/vvp.exe',str(out/'network-video.vvp')],cwd=root,env=env,capture_output=True,text=True,check=True)
assert 'NETWORK_VIDEO_RTL_PASS' in p.stdout
frames={};cpu=0
for line in (out/'network-video-packets.txt').read_text().splitlines():
    wire=bytes.fromhex(line.split()[1]);assert wire[:8]==b'\x55'*7+b'\xd5'
    packet=wire[8:-4];assert struct.unpack('<I',wire[-4:])[0]==binascii.crc32(packet)&0xffffffff
    if packet[:8]==bytes(range(8)):
        assert packet==bytes(range(100));cpu+=1;continue
    assert packet[:6]==bytes.fromhex('b025aa6c40df')
    ip=packet[14:34];words=struct.unpack('!10H',ip);total=sum(words)
    while total>>16:total=(total&65535)+(total>>16)
    assert total==65535 and ip[9]==17
    assert struct.unpack('!HH',packet[34:38])==(8090,8090)
    magic,frame,stamp,w,h,row,encoding=struct.unpack('!4sII4H',packet[42:62])
    assert (magic,w,h,encoding)==(b'V6V1',320 if full else 4,180 if full else 2,2)
    assert packet[62:]==b''.join(bytes((frame,(row*6+3)&255,(x*6+3)&255)) for x in range(w))
    frames.setdefault(frame,[]).append(row)
assert cpu==1 and sorted(frames)==([1] if full else [1,3,4,5,7,8,9,10,15,20,21,22]),frames
assert all(sorted(rows)==list(range(180 if full else 2)) for rows in frames.values())
report=dict(passed=True,physical_board_tested=False,arbitration_no_packet_interleaving=True,
            cpu_packet=True,video_frames=sorted(frames),exact_sampled_rgb=True,ethernet_crc=True,ip_checksum=True,
            two_receive_slots_overflow_counted=True,bad_rx_crc_rejected=True,rate_selection_79ms_source=not full,
            full_1920x1080_input=full,low_power_2fps_and_10fps_resume=not full)
(out/('network-video-rtl-full.json' if full else 'network-video-rtl.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
print('NETWORK_VIDEO_RTL_PASS',json.dumps(report))
