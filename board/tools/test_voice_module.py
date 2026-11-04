"""Standalone module check; --dry-run never opens a serial port."""
import argparse,time

def frame_for(text):
    payload=text.encode('gbk')
    if not 1<=len(payload)<=200:
        raise ValueError('GBK text must contain 1..200 bytes')
    frame=bytearray([0xfd,(len(payload)+3)>>8,(len(payload)+3)&255,1,1])+payload
    checksum=0
    for byte in frame:checksum^=byte
    return bytes(frame)+bytes([checksum])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',help='external USB UART attached to the voice module')
    parser.add_argument('--text',default='[v8][t5]系统已启动。')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    try:frame=frame_for(args.text)
    except (UnicodeEncodeError,ValueError) as error:parser.error(str(error))
    print('TX:',frame.hex(' '),'bytes:',len(frame))
    if args.dry_run:return
    if not args.port:parser.error('--port is required unless --dry-run is used')
    import serial
    port=serial.Serial(port=None,baudrate=9600,timeout=0.2,write_timeout=1)
    port.dtr=False;port.rts=False;port.port=args.port
    with port:
        port.reset_input_buffer();port.write(frame);port.flush()
        received=bytearray();deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            received.extend(port.read(32))
            if 0x45 in received:raise SystemExit('FAIL: module rejected command; RX '+received.hex(' '))
            if 0x41 in received and received[-1:]==b'\x4f':
                print('RX:',received.hex(' '),'accepted and playback finished; confirm actual sound separately')
                return
        raise SystemExit('No complete acceptance/playback response within 15s; RX '+received.hex(' '))

if __name__=='__main__':main()
