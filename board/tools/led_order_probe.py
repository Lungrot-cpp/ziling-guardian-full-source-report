"""Manually identify the real LED wiring; never inject detection or alarm data."""
import json
import queue
import threading
import urllib.parse
import urllib.request

BOARD = 'http://192.168.0.2'


def request(path, fields=None):
    data = None if fields is None else urllib.parse.urlencode(fields).encode('ascii')
    req = urllib.request.Request(BOARD + path, data=data)
    with urllib.request.urlopen(req, timeout=3) as response:
        return json.load(response)


def main():
    status = request('/api/care')
    if not status['hardware'] or 'led_test_index' not in status:
        raise RuntimeError('板端尚未加载灯序辨认程序。')
    changes = queue.Queue()
    stopped = threading.Event()

    def refresh():
        index = 0
        while not stopped.is_set():
            try:
                if not request('/api/care/action', {'cmd': 'led_test', 'index': index}).get('ok'):
                    raise RuntimeError('板卡没有接受灯号')
            except Exception as error:
                print('\n点灯未成功：' + str(error), flush=True)
            try:
                index = changes.get(timeout=1)
            except queue.Empty:
                pass

    print('灯序辨认：一次只亮一颗白灯，编号从 0 开始。')
    print('输入 0～255：指定灯号；回车/n：下一颗；p：上一颗；q：退出。')
    print('建议依次观察 0、1、15、16、31、32、255。')
    print('请记录：0在哪个角，0到1朝哪个方向，15到16是否折返。')
    print('退出或程序断开超过5秒，灯板恢复正常显示；照明和跌倒逻辑照常工作。')
    worker = threading.Thread(target=refresh, daemon=True)
    worker.start()
    index = 0
    try:
        while True:
            text = input(f'当前请求灯号 {index} > ').strip().lower()
            if text == 'q':
                break
            if text in ('', 'n'):
                candidate = (index + 1) % 256
            elif text == 'p':
                candidate = (index - 1) % 256
            elif text.isascii() and text.isdecimal():
                candidate = int(text)
            else:
                print('请输入灯号、n、p 或 q。')
                continue
            if not 0 <= candidate < 256:
                print('灯号范围为 0～255。')
                continue
            index = candidate
            changes.put(index)
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stopped.set()
        changes.put(index)
        worker.join(timeout=4)
        try:
            request('/api/care/action', {'cmd': 'led_test_stop'})
            print('\n已退出，恢复正常轨迹显示。')
        except Exception:
            print('\n连接中断，板端将在最后一次收到请求5秒后自动恢复。')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('无法启动灯序辨认：' + str(error))
        raise SystemExit(1)
