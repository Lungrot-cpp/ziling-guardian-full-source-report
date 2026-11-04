"""Read V12 board telemetry and send red-box alarms to a separate SYN6288 USB UART."""
import argparse
import json
import time
import zlib

from test_voice_module import frame_for

MESSAGE = '有人跌倒！请注意！'
ALARM_FRAME = frame_for('[v16]' + MESSAGE)
MAX_LINE = 65536
REPEAT_SECONDS = 15


def decode(line):
    header, length, checksum, body = line.rstrip(b'\r\n').split(b' ', 3)
    if (header != b'@V6' or len(body) != int(length) or len(body) > MAX_LINE
            or zlib.crc32(body) != int(checksum, 16)):
        raise ValueError('串口数据长度或CRC不正确')
    data = json.loads(body)
    if not isinstance(data, dict) or type(data.get('v')) is not int or data['v'] != 1:
        raise ValueError('不支持的板卡数据格式')
    if type(data.get('seq')) is not int or not 0 <= data['seq'] <= 0xffffffff:
        raise ValueError('无效的数据序号')
    tracks = data.get('tracks')
    if not isinstance(tracks, list) or len(tracks) > 16:
        raise ValueError('无效的目标列表')
    for track in tracks:
        if (not isinstance(track, dict) or type(track.get('state')) is not int
                or not 0 <= track['state'] <= 2):
            raise ValueError('无效的目标状态')
    return data


class TelemetryReader:
    def __init__(self):
        self.buffer = bytearray()
        self.discarding = False
        self.bad_frames = 0

    def feed(self, chunk):
        reports = []
        # Bound incomplete lines too; UART logs and corrupt frames must not trigger speech.
        for part in chunk.splitlines(keepends=True):
            self.buffer.extend(part)
            if len(self.buffer) > MAX_LINE:
                self.buffer.clear()
                self.discarding = True
            if not part.endswith(b'\n'):
                continue
            line = bytes(self.buffer)
            self.buffer.clear()
            if self.discarding:
                self.discarding = False
                self.bad_frames += 1
            elif line.startswith(b'@V6 '):
                try:
                    reports.append(decode(line))
                except (ValueError, TypeError, UnicodeError):
                    self.bad_frames += 1
        return reports


class Alarm:
    def __init__(self):
        self.last_seq = None
        self.last_sent = None

    def update(self, report, now, send):
        if report['seq'] == self.last_seq:
            return
        self.last_seq = report['seq']
        # state is the same visible_state used to draw the HDMI/web person box.
        # Historical events and alarm_latched alone do not mean a red box is visible now.
        if not any(track['state'] == 2 for track in report['tracks']):
            self.last_sent = None
        elif self.last_sent is None or now - self.last_sent >= REPEAT_SECONDS:
            send(ALARM_FRAME)
            self.last_sent = now


def open_uart(port, baud):
    import serial
    uart = serial.Serial(port=None, baudrate=baud, timeout=0.1, write_timeout=1)
    uart.dtr = False
    uart.rts = False
    uart.port = port
    uart.open()
    return uart


def choose_ports(args):
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    print('当前串口：', flush=True)
    for port in ports:
        print(f'  {port.device}: {port.description}', flush=True)
    if not ports:
        raise ValueError('没有发现串口，请连接USB转TTL和板卡原有USB串口')
    if not args.voice_port:
        args.voice_port = input('语音模块USB转TTL的COM口（本次测试为COM15）：').strip()
    if not args.test and not args.board_port:
        args.board_port = input('板卡原有USB串口的COM口（不能与语音口相同）：').strip()
    available = {p.device.upper() for p in ports}
    for port in [args.voice_port] + ([] if args.test else [args.board_port]):
        if port.upper() not in available:
            raise ValueError(f'未找到串口 {port}')
    if not args.test and args.board_port.upper() == args.voice_port.upper():
        raise ValueError('板卡与语音必须使用两个不同COM口')


def responses(voice):
    for value in voice.read(voice.in_waiting):
        text = {0x41: '已接收指令', 0x45: '拒绝指令，请检查协议/供电', 0x4f: '空闲状态'}.get(value)
        print(f'语音回包 {value:02X}: {text or "其他状态"}', flush=True)


def run(args):
    choose_ports(args)
    with open_uart(args.voice_port, 9600) as voice:
        voice.reset_input_buffer()

        def send(frame):
            if voice.write(frame) != len(frame):
                raise OSError('语音指令未完整发送')
            voice.flush()
            print(f'{time.strftime("%H:%M:%S")} 发送播报：{MESSAGE}', flush=True)

        if args.test:
            send(ALARM_FRAME)
            until = time.monotonic() + 10
            while time.monotonic() < until:
                responses(voice)
                time.sleep(0.05)
            return
        with open_uart(args.board_port, 460800) as board:
            board.reset_input_buffer()
            reader, alarm = TelemetryReader(), Alarm()
            last_valid, warned, received = time.monotonic(), False, False
            print(f'板卡 {args.board_port}/460800 -> 语音 {args.voice_port}/9600', flush=True)
            print('等待板卡数据；红框立即播报，持续红框每15秒重播。Ctrl+C退出。', flush=True)
            while True:
                chunk = board.read(min(max(board.in_waiting, 1), 4096))
                for report in reader.feed(chunk):
                    last_valid = time.monotonic()
                    if not received or warned:
                        print('已收到有效板卡数据，正在监测红框。', flush=True)
                    received, warned = True, False
                    alarm.update(report, last_valid, send)
                responses(voice)
                if not warned and time.monotonic() - last_valid >= 5:
                    print(f'5秒未收到有效板卡数据；检查板卡COM口/识别程序。已丢弃坏帧 {reader.bad_frames}。', flush=True)
                    warned = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board-port', help='板卡原有USB串口，例如COM7，460800波特率')
    parser.add_argument('--voice-port', help='SYN6288 USB转TTL串口，例如COM15，9600波特率')
    parser.add_argument('--test', action='store_true', help='仅播报一次指定报警句，不读取板卡')
    args = parser.parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print('\n已停止。')
    except (OSError, ValueError, EOFError, ImportError) as error:
        parser.exit(1, f'启动/运行失败：{error}\n请关闭占用COM口的串口助手或其他程序，检查连接后重新启动。\n')


if __name__ == '__main__':
    main()
