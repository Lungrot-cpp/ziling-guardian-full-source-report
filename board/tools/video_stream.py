"""Receive the V6 hardware RGB stream; reused from the 2026-10-16 receiver."""
import io
import socket
import struct
import threading
import time
from collections import deque
from PIL import Image, ImageDraw


def newer(a, b):
    return 0 < (a - b) % 2**32 < 2**31


class FrameAssembler:
    def __init__(self):
        self.pending = {}
        self.completed = None
        self.last_at = 0.0
        self.incomplete = 0

    def feed(self, packet, now):
        if len(packet) != 980:
            raise ValueError("video packet length")
        magic, frame, stamp, width, height, row, encoding = struct.unpack("!4sII4H", packet[:20])
        if (magic, width, height, encoding) != (b"V6V1", 320, 180, 2) or row >= height:
            raise ValueError("video packet header")
        if now - self.last_at > 2:
            self.pending.clear()
            self.completed = None
        self.last_at = now
        if self.completed is not None and not newer(frame, self.completed):
            return None
        for old in list(self.pending):
            if now - self.pending[old][2] > .5:
                del self.pending[old]
                self.incomplete += 1
        if frame not in self.pending:
            if len(self.pending) == 2:
                del self.pending[next(iter(self.pending))]
                self.incomplete += 1
            self.pending[frame] = (stamp, {}, now)
        previous_stamp, rows, _ = self.pending[frame]
        if previous_stamp != stamp or (row in rows and rows[row] != packet[20:]):
            del self.pending[frame]
            raise ValueError("inconsistent video frame")
        rows[row] = packet[20:]
        if len(rows) != height:
            return None
        pixels = b"".join(rows[y] for y in range(height))
        self.completed = frame
        self.incomplete += sum(k != frame and not newer(k, frame) for k in self.pending)
        self.pending = {k: v for k, v in self.pending.items() if newer(k, frame)}
        return frame, stamp, pixels


class VideoStream:
    def __init__(self, address, get_state):
        self.get_state = get_state
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        self.socket.bind(address)
        self.socket.settimeout(.2)
        self.condition = threading.Condition()
        self.running = True
        self.latest = None
        self.sequence = 0
        self.bad_packets = 0
        self.arrivals = deque(maxlen=30)
        self.target_fps = 10
        self.assembler = FrameAssembler()
        self.thread = threading.Thread(target=self.receive, daemon=True)
        self.thread.start()

    def receive(self):
        while self.running:
            try:
                packet, peer = self.socket.recvfrom(2048)
                if peer[0] != "192.168.0.2":
                    continue
                result = self.assembler.feed(packet, time.monotonic())
                if result is None:
                    continue
                frame, stamp, pixels = result
                image = Image.frombytes("RGB", (320, 180), pixels)
                state = self.get_state()
                target_fps = 2 if state.get("connected") and state.get("low_power") else 10
                age = ((stamp - state.get("capture_ms", 0) * 1000) % 2**32) / 1000
                if state.get("connected") and age <= 1500:
                    draw = ImageDraw.Draw(image)
                    for track in state["tracks"]:
                        x0, y0, x1, y1 = track["box"]
                        color = {"NORMAL": "#35c96c", "FALLING": "#ffc94b", "FALLEN": "#f44747"}[track["state"]]
                        draw.rectangle((round(x0*319), round(y0*179), round(x1*319), round(y1*179)), outline=color, width=2)
                        draw.text((round(x0*319)+3, max(0, round(y0*179)-12)), str(track["id"]), fill="white")
                output = io.BytesIO()
                image.save(output, "JPEG", quality=85)
                with self.condition:
                    if self.target_fps != target_fps:
                        self.target_fps = target_fps
                        self.arrivals.clear()
                    self.sequence += 1
                    self.latest = (frame, stamp, output.getvalue(), time.monotonic())
                    self.arrivals.append(self.latest[3])
                    self.condition.notify_all()
            except socket.timeout:
                continue
            except ValueError:
                self.bad_packets += 1
            except OSError:
                if self.running:
                    self.bad_packets += 1

    def info(self):
        with self.condition:
            age = (time.monotonic() - self.latest[3]) if self.latest else None
            online = age is not None and age < 1.5
            fps = (len(self.arrivals)-1)/(self.arrivals[-1]-self.arrivals[0]) if online and len(self.arrivals)>1 else 0
            return {"mode": "live", "kind": "mjpeg", "source": "v6-ethernet", "url": "/api/stream.mjpg" if online else "",
                    "connected": online, "width": 320, "height": 180, "fps": round(fps, 1),
                    "target_fps": self.target_fps, "low_power": self.target_fps == 2,
                    "age_ms": round(age*1000) if age is not None else None,
                    "frame_id": self.latest[0] if self.latest else None, "capture_us": self.latest[1] if self.latest else None,
                    "frames": self.sequence, "bad_packets": self.bad_packets, "incomplete_frames": self.assembler.incomplete}

    def close(self):
        self.running = False
        self.socket.close()
        with self.condition:
            self.condition.notify_all()
        self.thread.join(timeout=1)
